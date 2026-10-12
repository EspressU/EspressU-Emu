#pragma once

#include <cstdint>
#include <cstdio>

namespace StallDetect
{
	enum class Kind : int
	{
		None = 0,
		Picture = 1,
		GpuFault = 2,
		ScreenMemory = 3,
		LowMemory = 4,
		ScreenStopped = 5,
	};

	enum class Rule : uint8_t
	{
		None,
		CbErrorLatched,
		CbErrorStreak,
		GpuQueue,
		PresentFailing,
		GpuThreadNoProgress,
		LowMemory,
		GuestNotSending,
	};

	enum class WaitClass : uint8_t
	{
		None = 0,
		GuestIdle = 1,
		GuestWait = 2,
		Gpu = 3,
		Display = 4,
	};

	enum class Action : uint8_t
	{
		None,
		Suspect,
		SuspectDropped,
		Raise,
		Clear,
		GuestNote,
	};

	struct Thresholds
	{
		double refFrameMs = 1000.0 / 30.0;
		double maxFrameScale = 4.0;
		uint32_t minFrameSamples = 16;

		int64_t settleTitleStartMs = 15000;
		int64_t settleResumeMs = 3000;
		int64_t hiccupMs = 1500;

		int64_t confirmMs = 2000;
		int64_t queueWindowMs = 8000;
		int64_t presentWindowMs = 8000;
		int64_t unknownWindowMs = 15000;
		int64_t unknownBootWindowMs = 45000;
		int64_t streakWindowMs = 3000;
		int64_t streakConfirmMs = 1000;
		uint32_t streakMin = 5;

		uint32_t failuresInARowMin = 30;
		int64_t failureFreshMs = 1500;
		double screenMemoryFraction = 0.10;

		int64_t guestNoteMs = 10000;
		int64_t guestNoteRepeatMs = 60000;
		int64_t guestRecentMs = 2000;

		double lowMemFraction = 0.04;
		double lowMemClearFraction = 0.07;
		uint32_t memFloorMB = 32;
		int64_t lowMemHoldMs = 3000;
	};

	struct Sample
	{
		int64_t nowMs = 0;
		bool titleRunning = false;
		bool gx2Init = false;
		bool paused = false;
		bool appActive = true;
		uint32_t layoutStamp = 0;
		double windowScale = 1.0;
		uint32_t frames = 0;
		uint32_t presented = 0;
		uint32_t drawableFailures = 0;
		uint32_t drawableFailuresInARow = 0;
		uint32_t cbRetired = 0;
		uint32_t cbErrorStreak = 0;
		uint32_t pm4 = 0;
		uint32_t draws = 0;
		uint32_t flipRequests = 0;
		uint32_t evictionPasses = 0;
		bool gpuError = false;
		int32_t gpuErrorCode = 0;
		int32_t lastCbErrorCode = 0;
		bool gpuPresumedLost = false;
		WaitClass waitClass = WaitClass::None;
		uint32_t availMemMB = 0;
		uint32_t memLimitMB = 0;
	};

	struct Decision
	{
		Action action = Action::None;
		Kind kind = Kind::None;
		Rule rule = Rule::None;
		char text[700] = {0};
	};

	inline const char* RuleName(Rule r)
	{
		switch (r)
		{
		case Rule::CbErrorLatched: return "cb_error_latched";
		case Rule::CbErrorStreak: return "cb_error_streak";
		case Rule::GpuQueue: return "gpu_queue_stalled";
		case Rule::PresentFailing: return "present_failing";
		case Rule::GpuThreadNoProgress: return "gpu_thread_no_progress";
		case Rule::LowMemory: return "low_memory";
		case Rule::GuestNotSending: return "guest_not_sending";
		default: return "none";
		}
	}

	inline const char* WaitName(WaitClass w)
	{
		switch (w)
		{
		case WaitClass::GuestIdle: return "guest idle";
		case WaitClass::GuestWait: return "guest wait";
		case WaitClass::Gpu: return "GPU wait";
		case WaitClass::Display: return "drawable wait";
		default: return "running/unknown";
		}
	}

	class Detector
	{
	public:
		explicit Detector(const Thresholds& t = Thresholds()) : t_(t) { Reset(0, true); }

		void Reset(int64_t nowMs, bool titleStart)
		{
			if (frameCount_ >= t_.minFrameSamples)
				priorFrameMs_ = MedianFrameMs();
			frameCount_ = 0;
			framePos_ = 0;
			stallKind_ = Kind::None;
			stallRule_ = Rule::None;
			raisedAtMs_ = 0;
			memRaised_ = false;
			memLowSince_ = -1;
			have_ = false;
			suspectRule_ = Rule::None;
			suspectSinceMs_ = 0;
			lastNowMs_ = nowMs;
			settleUntilMs_ = nowMs + (int64_t)((titleStart ? t_.settleTitleStartMs : t_.settleResumeMs) * FrameScale());
		}

		Kind kind() const { return stallKind_ != Kind::None ? stallKind_ : (memRaised_ ? Kind::LowMemory : Kind::None); }
		Rule rule() const { return stallKind_ != Kind::None ? stallRule_ : (memRaised_ ? Rule::LowMemory : Rule::None); }
		int64_t raisedAtMs() const { return raisedAtMs_; }
		bool stallRaised() const { return stallKind_ != Kind::None; }

		double FrameScale() const
		{
			double m = frameCount_ >= t_.minFrameSamples ? MedianFrameMs() : priorFrameMs_;
			if (m <= 0.0)
				return 1.0;
			double f = m / t_.refFrameMs;
			if (f < 1.0) f = 1.0;
			if (f > t_.maxFrameScale) f = t_.maxFrameScale;
			return f;
		}
		double MedianFrameMsForLog() const { return frameCount_ >= t_.minFrameSamples ? MedianFrameMs() : priorFrameMs_; }

		Decision Update(const Sample& s)
		{
			Decision d;
			const int64_t now = s.nowMs;

			if (!s.titleRunning)
			{
				d = ClearStall(s, "title is not running");
				have_ = false;
				lastNowMs_ = now;
				return d;
			}

			if (s.gpuError)
			{
				if (stallRule_ != Rule::CbErrorLatched)
				{
					char body[300];
					std::snprintf(body, sizeof(body), "a command buffer failed with code %d and iOS now ignores this app's GPU work; definite, shown immediately (no window, no confirmation)", (int)s.gpuErrorCode);
					d = Raise(s, Kind::GpuFault, Rule::CbErrorLatched, body);
				}
				lastNowMs_ = now;
				return d;
			}
			if (stallRule_ == Rule::CbErrorLatched)
			{
				d = ClearStall(s, "the GPU fault state was reset");
				have_ = false;
				lastNowMs_ = now;
				return d;
			}

			const bool expected = s.gx2Init && !s.paused && s.appActive;
			if (!expected)
			{
				d = ClearStall(s, !s.gx2Init ? "the title has not initialised graphics" : (s.paused ? "the title is paused" : "the app is not active"));
				have_ = false;
				suspectRule_ = Rule::None;
				lastNowMs_ = now;
				return d;
			}

			if (!have_)
			{
				Rebaseline(s, (int64_t)(t_.settleResumeMs * ScaleFor(s)));
				lastNowMs_ = now;
				return d;
			}
			if (now - lastNowMs_ > t_.hiccupMs || s.layoutStamp != layoutStamp_)
			{
				Rebaseline(s, (int64_t)(t_.settleResumeMs * ScaleFor(s)));
				lastNowMs_ = now;
				return d;
			}
			lastNowMs_ = now;

			Track(s);

			if (stallKind_ != Kind::None)
				return CheckRecovery(s);

			if (now < settleUntilMs_)
			{
				suspectRule_ = Rule::None;
				return d;
			}

			const double scale = ScaleFor(s);
			Candidate c = Evaluate(s, scale);
			if (c.rule != Rule::None)
			{
				if (suspectRule_ != c.rule)
				{
					suspectRule_ = c.rule;
					suspectSinceMs_ = now;
					d.action = Action::Suspect;
					d.kind = c.kind;
					d.rule = c.rule;
					std::snprintf(d.text, sizeof(d.text), "rule=%s SUSPECTED, confirming for %.1f s: %s", RuleName(c.rule), (double)c.confirmMs / 1000.0, c.body);
					return d;
				}
				if (now - suspectSinceMs_ >= c.confirmMs)
				{
					suspectRule_ = Rule::None;
					return Raise(s, c.kind, c.rule, c.body);
				}
				return d;
			}
			if (suspectRule_ != Rule::None)
			{
				d.action = Action::SuspectDropped;
				d.rule = suspectRule_;
				std::snprintf(d.text, sizeof(d.text), "rule=%s suspicion dropped after %.1f s: the condition no longer holds (progress resumed)", RuleName(suspectRule_), (double)(now - suspectSinceMs_) / 1000.0);
				suspectRule_ = Rule::None;
				return d;
			}

			return GuestNote(s, scale);
		}

		Decision UpdateMemory(const Sample& s)
		{
			Decision d;
			const int64_t now = s.nowMs;
			const uint32_t warnMB = MemThresholdMB(s, t_.lowMemFraction);
			const uint32_t clearMB = MemThresholdMB(s, t_.lowMemClearFraction);
			const bool judge = s.titleRunning && s.appActive && s.availMemMB != 0 && warnMB != 0;
			if (!judge)
			{
				memLowSince_ = -1;
				if (memRaised_ && !(s.titleRunning && s.appActive))
				{
					memRaised_ = false;
					d.action = Action::Clear;
					d.rule = Rule::LowMemory;
					std::snprintf(d.text, sizeof(d.text), "rule=low_memory cleared: the title or the app is no longer in the foreground");
				}
				return d;
			}
			if (memRaised_)
			{
				if (s.availMemMB >= clearMB)
				{
					memRaised_ = false;
					memLowSince_ = -1;
					d.action = Action::Clear;
					d.rule = Rule::LowMemory;
					std::snprintf(d.text, sizeof(d.text), "rule=low_memory cleared: %u MB free (clear threshold %u MB = %.0f%% of the %u MB limit)", (unsigned)s.availMemMB, (unsigned)clearMB, t_.lowMemClearFraction * 100.0, (unsigned)s.memLimitMB);
				}
				return d;
			}
			if (s.availMemMB < warnMB)
			{
				if (memLowSince_ < 0)
					memLowSince_ = now;
				if (now - memLowSince_ >= t_.lowMemHoldMs)
				{
					memRaised_ = true;
					d.action = Action::Raise;
					d.kind = Kind::LowMemory;
					d.rule = Rule::LowMemory;
					std::snprintf(d.text, sizeof(d.text), "rule=low_memory CONFIRMED: %u MB free for %.1f s (threshold < %u MB = %.0f%% of the %u MB memory limit, held >= %.1f s); this is a warning, the picture may still be moving",
						(unsigned)s.availMemMB, (double)(now - memLowSince_) / 1000.0, (unsigned)warnMB, t_.lowMemFraction * 100.0, (unsigned)s.memLimitMB, (double)t_.lowMemHoldMs / 1000.0);
				}
			}
			else
			{
				memLowSince_ = -1;
			}
			return d;
		}

	private:
		struct Candidate
		{
			Rule rule = Rule::None;
			Kind kind = Kind::None;
			int64_t confirmMs = 0;
			char body[520] = {0};
		};

		uint32_t MemThresholdMB(const Sample& s, double fraction) const
		{
			if (s.memLimitMB == 0)
				return 0;
			const uint32_t mb = (uint32_t)((double)s.memLimitMB * fraction);
			return mb < t_.memFloorMB ? t_.memFloorMB : mb;
		}

		static double Sec(int64_t ms) { return (double)ms / 1000.0; }

		double ScaleFor(const Sample& s) const
		{
			const double thermal = s.windowScale < 1.0 ? 1.0 : s.windowScale;
			return thermal * FrameScale();
		}

		void ScaleNote(const Sample& s, double scale, char* out, size_t cap) const
		{
			std::snprintf(out, cap, "windows x%.2f = median frame %.0f ms vs %.0f ms reference x%.2f, thermal x%.1f", scale, MedianFrameMsForLog(), t_.refFrameMs, FrameScale(), s.windowScale < 1.0 ? 1.0 : s.windowScale);
		}

		double MedianFrameMs() const
		{
			double v[kFrameRing];
			const uint32_t n = frameCount_ < kFrameRing ? frameCount_ : kFrameRing;
			for (uint32_t i = 0; i < n; i++) v[i] = frameMs_[i];
			for (uint32_t i = 1; i < n; i++)
			{
				double x = v[i]; uint32_t j = i;
				while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
				v[j] = x;
			}
			return n == 0 ? 0.0 : ((n & 1) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]));
		}

		void Rebaseline(const Sample& s, int64_t settleMs)
		{
			const int64_t now = s.nowMs;
			have_ = true;
			prevFrames_ = s.frames; tFrames_ = now;
			prevPresented_ = s.presented; tPresented_ = now;
			prevRetired_ = s.cbRetired; tRetired_ = now;
			prevPm4_ = s.pm4; tPm4_ = now;
			prevDraws_ = s.draws;
			prevFlips_ = s.flipRequests; tFlips_ = now;
			prevFailures_ = s.drawableFailures; tFailure_ = now;
			prevEvictions_ = s.evictionPasses;
			presumedLostSince_ = s.gpuPresumedLost ? now : -1;
			streakSince_ = -1;
			layoutStamp_ = s.layoutStamp;
			lastGuestWaitMs_ = -1000000000LL;
			idleNotedAtMs_ = -1;
			suspectRule_ = Rule::None;
			if (now + settleMs > settleUntilMs_)
				settleUntilMs_ = now + settleMs;
		}

		void Track(const Sample& s)
		{
			const int64_t now = s.nowMs;
			if (s.frames != prevFrames_)
			{
				const uint32_t n = s.frames - prevFrames_;
				if (n > 0 && n < 100000 && now > tFrames_)
				{
					frameMs_[framePos_] = (double)(now - tFrames_) / (double)n;
					framePos_ = (framePos_ + 1) % kFrameRing;
					if (frameCount_ < kFrameRing) frameCount_++;
				}
				prevFrames_ = s.frames; tFrames_ = now; idleNotedAtMs_ = -1;
			}
			if (s.presented != prevPresented_) { prevPresented_ = s.presented; tPresented_ = now; }
			if (s.cbRetired != prevRetired_) { prevRetired_ = s.cbRetired; tRetired_ = now; }
			if (s.pm4 != prevPm4_) { prevPm4_ = s.pm4; tPm4_ = now; }
			if (s.draws != prevDraws_) { prevDraws_ = s.draws; tPm4_ = now; }
			if (s.flipRequests != prevFlips_) { prevFlips_ = s.flipRequests; tFlips_ = now; }
			if (s.drawableFailures != prevFailures_) { prevFailures_ = s.drawableFailures; tFailure_ = now; }
			if (s.evictionPasses != prevEvictions_) { prevEvictions_ = s.evictionPasses; tPm4_ = now; }
			if (s.gpuPresumedLost) { if (presumedLostSince_ < 0) presumedLostSince_ = now; }
			else presumedLostSince_ = -1;
			if (s.cbErrorStreak >= t_.streakMin) { if (streakSince_ < 0) streakSince_ = now; }
			else streakSince_ = -1;
			if (s.waitClass == WaitClass::GuestIdle || s.waitClass == WaitClass::GuestWait)
				lastGuestWaitMs_ = now;
		}

		Candidate Evaluate(const Sample& s, double scale) const
		{
			Candidate c;
			const int64_t now = s.nowMs;
			const int64_t ageFrames = now - tFrames_, agePresent = now - tPresented_, ageRetired = now - tRetired_;
			const int64_t agePm4 = now - tPm4_, ageFlips = now - tFlips_, ageFailure = now - tFailure_;
			const bool guestRecent = (now - lastGuestWaitMs_) < t_.guestRecentMs;

			char note[160];
			ScaleNote(s, scale, note, sizeof(note));
			const int64_t wsk = (int64_t)(t_.streakWindowMs * scale);
			if (streakSince_ >= 0 && now - streakSince_ >= wsk)
			{
				c.rule = Rule::CbErrorStreak;
				c.kind = Kind::GpuFault;
				c.confirmMs = (int64_t)(t_.streakConfirmMs * scale);
				std::snprintf(c.body, sizeof(c.body), "%u command buffers in a row failed (last code %d), none succeeded for %.1f s (need >= %u in a row held >= %.1f s) while the app is in the foreground [%s]",
					(unsigned)s.cbErrorStreak, (int)s.lastCbErrorCode, Sec(now - streakSince_), (unsigned)t_.streakMin, Sec(wsk), note);
				return c;
			}

			const int64_t wq = (int64_t)(t_.queueWindowMs * scale);
			if (presumedLostSince_ >= 0 && now - presumedLostSince_ >= wq && ageRetired >= wq && agePresent >= wq
				&& (agePm4 < wq || ageFlips < wq) && !guestRecent)
			{
				c.rule = Rule::GpuQueue;
				c.kind = Kind::Picture;
				c.confirmMs = (int64_t)(t_.confirmMs * scale);
				std::snprintf(c.body, sizeof(c.body), "GPU waits have been timing out for %.1f s, no command buffer finished for %.1f s, no frame presented for %.1f s (each >= %.1f s); the game is still submitting (last GPU packet %.1f s ago, last flip request %.1f s ago); GPU thread: %s [%s]",
					Sec(now - presumedLostSince_), Sec(ageRetired), Sec(agePresent), Sec(wq), Sec(agePm4), Sec(ageFlips), WaitName(s.waitClass), note);
				return c;
			}

			const int64_t wp = (int64_t)(t_.presentWindowMs * scale);
			const int64_t wfresh = (int64_t)(t_.failureFreshMs * scale);
			if (s.drawableFailuresInARow >= t_.failuresInARowMin && ageFailure < wfresh && agePresent >= wp)
			{
				c.rule = Rule::PresentFailing;
				const uint32_t screenMemMB = MemThresholdMB(s, t_.screenMemoryFraction);
				const bool lowMem = s.availMemMB != 0 && screenMemMB != 0 && s.availMemMB < screenMemMB;
				c.kind = lowMem ? Kind::ScreenMemory : Kind::ScreenStopped;
				c.confirmMs = (int64_t)(t_.confirmMs * scale);
				std::snprintf(c.body, sizeof(c.body), "%u drawable requests in a row failed (need >= %u), the latest %.1f s ago (< %.1f s), no frame presented for %.1f s (>= %.1f s); free memory %u MB of a %u MB limit (%s, memory cause below %u MB = %.0f%%) [%s]",
					(unsigned)s.drawableFailuresInARow, (unsigned)t_.failuresInARowMin, Sec(ageFailure), Sec(wfresh), Sec(agePresent), Sec(wp),
					(unsigned)s.availMemMB, (unsigned)s.memLimitMB, lowMem ? "low, counted as out of memory for the screen" : "not low, cause unknown", (unsigned)screenMemMB, t_.screenMemoryFraction * 100.0, note);
				return c;
			}

			const int64_t wu = (int64_t)((s.frames < 30 ? t_.unknownBootWindowMs : t_.unknownWindowMs) * scale);
			const bool guestSide = s.waitClass == WaitClass::GuestIdle || s.waitClass == WaitClass::GuestWait || guestRecent;
			if (ageFrames >= wu && agePm4 >= wu && agePresent >= wu && ageRetired >= wu && !guestSide)
			{
				c.rule = Rule::GpuThreadNoProgress;
				c.kind = Kind::Picture;
				c.confirmMs = (int64_t)(t_.confirmMs * scale);
				std::snprintf(c.body, sizeof(c.body), "no frame for %.1f s, no GPU packet or draw call for %.1f s, nothing finished for %.1f s, nothing presented for %.1f s (each >= %.1f s) and the GPU thread is not waiting for the game (%s); cause unverified [%s]",
					Sec(ageFrames), Sec(agePm4), Sec(ageRetired), Sec(agePresent), Sec(wu), WaitName(s.waitClass), note);
				return c;
			}
			return c;
		}

		Decision GuestNote(const Sample& s, double scale)
		{
			Decision d;
			const int64_t now = s.nowMs;
			const int64_t ageFrames = now - tFrames_;
			const int64_t wn = (int64_t)(t_.guestNoteMs * scale);
			const bool guestSide = s.waitClass == WaitClass::GuestIdle || s.waitClass == WaitClass::GuestWait || (now - lastGuestWaitMs_) < t_.guestRecentMs;
			if (ageFrames < wn || !guestSide)
				return d;
			if (idleNotedAtMs_ >= 0 && now - idleNotedAtMs_ < t_.guestNoteRepeatMs)
				return d;
			idleNotedAtMs_ = now;
			char note[160];
			ScaleNote(s, scale, note, sizeof(note));
			d.action = Action::GuestNote;
			d.rule = Rule::GuestNotSending;
			std::snprintf(d.text, sizeof(d.text), "rule=guest_not_sending: no frame for %.1f s (note threshold %.1f s) but the GPU thread is waiting on the game (%s), last GPU packet %.1f s ago, last flip request %.1f s ago; a game-side load or hang, NOT a video freeze, no card shown [%s]",
				Sec(ageFrames), Sec(wn), WaitName(s.waitClass), Sec(now - tPm4_), Sec(now - tFlips_), note);
			return d;
		}

		Decision Raise(const Sample& s, Kind kind, Rule rule, const char* body)
		{
			Decision d;
			stallKind_ = kind;
			stallRule_ = rule;
			raisedAtMs_ = s.nowMs;
			raiseFrames_ = s.frames;
			raisePresented_ = s.presented;
			raiseRetired_ = s.cbRetired;
			raisePm4_ = s.pm4;
			raiseDraws_ = s.draws;
			d.action = Action::Raise;
			d.kind = kind;
			d.rule = rule;
			std::snprintf(d.text, sizeof(d.text), "rule=%s CONFIRMED: %s", RuleName(rule), body);
			return d;
		}

		Decision ClearStall(const Sample& s, const char* why)
		{
			Decision d;
			if (stallKind_ == Kind::None)
				return d;
			d.action = Action::Clear;
			d.kind = Kind::None;
			d.rule = stallRule_;
			std::snprintf(d.text, sizeof(d.text), "rule=%s cleared after %.1f s: %s", RuleName(stallRule_), Sec(s.nowMs - raisedAtMs_), why);
			stallKind_ = Kind::None;
			stallRule_ = Rule::None;
			return d;
		}

		Decision CheckRecovery(const Sample& s)
		{
			bool recovered = false;
			char why[200];
			switch (stallRule_)
			{
			case Rule::CbErrorStreak:
				recovered = s.cbErrorStreak < t_.streakMin || s.cbRetired != raiseRetired_;
				std::snprintf(why, sizeof(why), "frames are back: a command buffer finished again (finished %u -> %u, error streak %u)", (unsigned)raiseRetired_, (unsigned)s.cbRetired, (unsigned)s.cbErrorStreak);
				break;
			case Rule::GpuQueue:
				recovered = s.cbRetired != raiseRetired_ || s.presented != raisePresented_;
				std::snprintf(why, sizeof(why), "frames are back: finished %u -> %u, presented %u -> %u", (unsigned)raiseRetired_, (unsigned)s.cbRetired, (unsigned)raisePresented_, (unsigned)s.presented);
				break;
			case Rule::PresentFailing:
				recovered = s.presented != raisePresented_;
				std::snprintf(why, sizeof(why), "frames are back: presented %u -> %u", (unsigned)raisePresented_, (unsigned)s.presented);
				break;
			default:
				recovered = s.frames != raiseFrames_ || s.pm4 != raisePm4_ || s.draws != raiseDraws_ || s.presented != raisePresented_ || s.cbRetired != raiseRetired_;
				std::snprintf(why, sizeof(why), "frames are back: frame %u -> %u, GPU packets %u -> %u, presented %u -> %u", (unsigned)raiseFrames_, (unsigned)s.frames, (unsigned)raisePm4_, (unsigned)s.pm4, (unsigned)raisePresented_, (unsigned)s.presented);
				break;
			}
			if (!recovered)
				return Decision();
			Decision d = ClearStall(s, why);
			Rebaseline(s, (int64_t)(t_.settleResumeMs * ScaleFor(s)));
			return d;
		}

		static constexpr uint32_t kFrameRing = 64;
		double frameMs_[kFrameRing] = {0};
		uint32_t frameCount_ = 0, framePos_ = 0;
		double priorFrameMs_ = 0.0;

		Thresholds t_;
		Kind stallKind_ = Kind::None;
		Rule stallRule_ = Rule::None;
		int64_t raisedAtMs_ = 0;
		uint32_t raiseFrames_ = 0, raisePresented_ = 0, raiseRetired_ = 0, raisePm4_ = 0, raiseDraws_ = 0;
		bool memRaised_ = false;
		int64_t memLowSince_ = -1;

		bool have_ = false;
		int64_t lastNowMs_ = 0;
		int64_t settleUntilMs_ = 0;
		uint32_t layoutStamp_ = 0;
		uint32_t prevFrames_ = 0, prevPresented_ = 0, prevRetired_ = 0, prevPm4_ = 0, prevDraws_ = 0, prevFlips_ = 0, prevFailures_ = 0, prevEvictions_ = 0;
		int64_t tFrames_ = 0, tPresented_ = 0, tRetired_ = 0, tPm4_ = 0, tFlips_ = 0, tFailure_ = 0;
		int64_t presumedLostSince_ = -1, streakSince_ = -1;
		int64_t lastGuestWaitMs_ = -1000000000LL;
		int64_t idleNotedAtMs_ = -1;
		Rule suspectRule_ = Rule::None;
		int64_t suspectSinceMs_ = 0;
	};
}
