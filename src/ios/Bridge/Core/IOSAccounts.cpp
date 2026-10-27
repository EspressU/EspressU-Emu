#include "Cafe/Account/Account.h"
#include "Cafe/CafeSystem.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"

#include <boost/nowide/convert.hpp>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
	std::string SanitizeField(std::string_view value)
	{
		std::string result;
		result.reserve(value.size());
		for (char c : value)
		{
			if (c != '\x1E' && c != '\x1F')
				result.push_back(c);
		}
		return result;
	}

	const Account* FindAccount(uint32 persistentId)
	{
		for (const auto& account : Account::GetAccounts())
		{
			if (account.GetPersistentId() == persistentId)
				return &account;
		}
		return nullptr;
	}

	template <typename Mutator>
	bool MutateAccount(uint32 persistentId, Mutator&& mutator)
	{
		Account account(Account::GetFileName(persistentId).wstring());
		if (account.Load())
			return false;
		mutator(account);
		if (account.Save())
			return false;
		Account::RefreshAccounts();
		return true;
	}
}

void IOSAccounts_Refresh()
{
	Account::RefreshAccounts();
}

std::string IOSAccounts_List()
{
	std::ostringstream out;
	const auto& accounts = Account::GetAccounts();
	for (size_t i = 0; i < accounts.size(); i++)
	{
		if (i != 0)
			out << '\x1E';
		const auto& account = accounts[i];
		out << fmt::format("{:08x}", account.GetPersistentId()) << '\x1F'
			<< SanitizeField(boost::nowide::narrow(std::wstring(account.GetMiiName()))) << '\x1F'
			<< account.GetBirthYear() << '\x1F'
			<< (int)account.GetBirthMonth() << '\x1F'
			<< (int)account.GetBirthDay() << '\x1F'
			<< (int)account.GetGender() << '\x1F'
			<< SanitizeField(account.GetEmail()) << '\x1F'
			<< account.GetCountry() << '\x1F'
			<< '0';
	}
	return out.str();
}

bool IOSAccounts_HasFreeSlot()
{
	return Account::HasFreeAccountSlots();
}

uint32_t IOSAccounts_NextPersistentId()
{
	return Account::GetNextPersistentId();
}

uint32_t IOSAccounts_MinPersistentId()
{
	return Account::kMinPersistendId;
}

bool IOSAccounts_Locked()
{
	return CafeSystem::IsTitleRunning();
}

bool IOSAccounts_Create(uint32_t persistentId, const char* miiName, uint16_t birthYear,
	uint8_t birthMonth, uint8_t birthDay, int gender, const char* email, int country)
{
	Account::RefreshAccounts();

	if (!Account::HasFreeAccountSlots())
		return false;
	if (persistentId < Account::kMinPersistendId)
		return false;
	if (FindAccount(persistentId) != nullptr)
		return false;
	if (!miiName || miiName[0] == '\0')
		return false;

	try
	{
		const std::wstring wideName = boost::nowide::widen(miiName);
		Account account(persistentId, wideName);
		account.SetBirthYear(birthYear);
		account.SetBirthMonth(birthMonth);
		account.SetBirthDay(birthDay);
		account.SetGender((uint8)gender);
		if (email)
			account.SetEmail(email);
		account.SetCountry((uint32)country);

		if (account.Save())
			return false;
	}
	catch (const std::exception&)
	{
		return false;
	}

	Account::RefreshAccounts();
	return true;
}

bool IOSAccounts_Delete(uint32_t persistentId)
{
	Account::RefreshAccounts();

	if (Account::GetAccounts().size() == 1)
		return false;
	if (FindAccount(persistentId) == nullptr)
		return false;

	const fs::path path = Account::GetFileName(persistentId).parent_path();
	std::error_code ec;
	fs::remove_all(path, ec);
	if (ec)
		return false;

	Account::RefreshAccounts();
	return true;
}

bool IOSAccounts_SetMiiName(uint32_t persistentId, const char* miiName)
{
	if (!miiName || miiName[0] == '\0')
		return false;
	return MutateAccount(persistentId, [&](Account& account) {
		account.SetMiiName(boost::nowide::widen(miiName));
	});
}

bool IOSAccounts_SetGender(uint32_t persistentId, int gender)
{
	return MutateAccount(persistentId, [&](Account& account) {
		account.SetGender((uint8)gender);
	});
}

uint32_t IOSAccounts_ActivePersistentId()
{
	return GetConfig().account.m_persistent_id.GetValue();
}

void IOSAccounts_SetActivePersistentId(uint32_t persistentId)
{
	GetConfig().account.m_persistent_id = persistentId;
	GetConfigHandle().Save();
}

