#include "LaunchSettings.h"

#include "util/helpers/helpers.h"
#include "Cafe/Account/Account.h"

#include "Cafe/OS/libs/coreinit/coreinit.h"

#include "boost/program_options.hpp"

#include "config/ActiveSettings.h"
#include "config/NetworkSettings.h"
#include "util/crypto/aes128.h"

#include "Cafe/Filesystem/FST/fstUtil.h"
#include "util/helpers/StringHelpers.h"

void requireConsole();

bool LaunchSettings::HandleCommandline(const wchar_t* lpCmdLine)
{
	#if BOOST_OS_WINDOWS
	const std::vector<std::wstring> args = boost::program_options::split_winmain(lpCmdLine);
	return HandleCommandline(args);
	#else
	cemu_assert_unimplemented();
	return false;
	#endif
}

bool LaunchSettings::HandleCommandline(int argc, wchar_t* argv[])
{
	std::vector<std::wstring> args;
	args.reserve(argc);
	for(int i = 0; i < argc; ++i)
	{
		args.emplace_back(argv[i]);
	}

	return HandleCommandline(args);
}

bool LaunchSettings::HandleCommandline(int argc, char* argv[])
{
	std::vector<std::wstring> args;
	args.reserve(argc);
	for(int i = 0; i < argc; ++i)
	{
		args.emplace_back(boost::nowide::widen(argv[i]));
	}

	return HandleCommandline(args);
}

bool LaunchSettings::HandleCommandline(const std::vector<std::wstring>& args)
{
	namespace po = boost::program_options;
	po::options_description desc{ "Launch options" };
	desc.add_options()
		("help,h", "This help screen")
		("version,v", "Displays the version of Cemu")
#if !BOOST_OS_WINDOWS
		("verbose", "Log to stdout")
#endif

		("game,g", po::wvalue<std::wstring>(), "Path of game to launch")
        ("title-id,t", po::value<std::string>(), "Title ID of the title to be launched (overridden by --game)")
		("mlc,m", po::wvalue<std::wstring>(), "Custom mlc folder location")

		("fullscreen,f", po::value<bool>()->implicit_value(true), "Launch games in fullscreen mode")
		("ud,u", po::value<bool>()->implicit_value(true), "Render output upside-down")

		("account,a", po::value<std::string>(), "Persistent id of account")

		("force-interpreter", po::value<bool>()->implicit_value(true), "Force interpreter CPU emulation. Useful for debugging purposes where you want to get accurate memory accesses and stack traces.")
		("force-multicore-interpreter", po::value<bool>()->implicit_value(true), "Force multi-core interpreter CPU emulation. Only useful for getting stack traces, but slightly faster than the single-core interpreter mode.");

	po::options_description hidden{ "Hidden options" };
	hidden.add_options()
		("nsight", po::value<bool>()->implicit_value(true), "NSight debugging options")
		("legacy", po::value<bool>()->implicit_value(true), "Intel legacy graphic mode");


	po::options_description all;
	all.add(desc).add(hidden);

	po::options_description visible;
	visible.add(desc);

	try
	{
		po::wcommand_line_parser parser{ args };
		parser.allow_unregistered().options(all).style(po::command_line_style::allow_long | po::command_line_style::allow_short | po::command_line_style::allow_dash_for_short | po::command_line_style::case_insensitive |
			po::command_line_style::long_allow_next |
			po::command_line_style::short_allow_next |
			po::command_line_style::allow_long_disguise);

		const auto parsed_options = parser.run();

		po::variables_map vm;
		po::store(parsed_options, vm);
		notify(vm);

		if (vm.count("help"))
		{
			requireConsole();
			std::cout << visible << std::endl;
			return false;
		}
		if (vm.count("version"))
		{
			requireConsole();
			std::string versionStr;
#if EMULATOR_VERSION_PATCH == 0
			versionStr = fmt::format("{}.{}{}", EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_SUFFIX);
#else
			versionStr = fmt::format("{}.{}-{}{}", EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH, EMULATOR_VERSION_SUFFIX);
#endif
			std::cout << versionStr << std::endl;
			return false;
		}

		if (vm.count("verbose"))
			s_verbose = true;

		if (vm.count("game"))
		{
			std::wstring tmp = vm["game"].as<std::wstring>();
			if (tmp.size() > 0 && tmp.front() == '=')
				tmp.erase(tmp.begin()+0);

			s_load_game_file = tmp;
		}
        if (vm.count("title-id"))
        {
            auto title_param = vm["title-id"].as<std::string>();
            try {

                if (title_param.starts_with('=')){
                    title_param.erase(title_param.begin());
                }
                s_load_title_id = std::stoull(title_param, nullptr, 16);
            }
            catch (std::invalid_argument const& e)
            {
                std::cerr << "Expected title_param ID as an unsigned 64-bit hexadecimal string\n";
            }
        }

		if (vm.count("mlc"))
		{
			std::wstring tmp = vm["mlc"].as<std::wstring>();
			if (tmp.size() > 0 && tmp.front() == '=')
				tmp.erase(tmp.begin() + 0);

			s_mlc_path = tmp;
		}

		if (vm.count("account"))
		{
			const auto id = ConvertString<uint32>(vm["account"].as<std::string>(), 16);
			if (id >= Account::kMinPersistendId)
				s_persistent_id = id;
		}

		if (vm.count("fullscreen"))
			s_fullscreen = vm["fullscreen"].as<bool>();
		if (vm.count("ud"))
			s_render_upside_down = vm["ud"].as<bool>();

		if (vm.count("nsight"))
			s_nsight_mode = vm["nsight"].as<bool>();

		if(vm.count("force-interpreter"))
			s_force_interpreter = vm["force-interpreter"].as<bool>();

		if(vm.count("force-multicore-interpreter"))
			s_force_multicore_interpreter = vm["force-multicore-interpreter"].as<bool>();

		return true;
	}
	catch (const std::exception& ex)
	{
		std::string errorMsg;
		errorMsg.append("Error while trying to parse command line parameter:\n");
		errorMsg.append(ex.what());
		std::cout << errorMsg << std::endl;
		return false;
	}

}

