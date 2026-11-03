# Contributing to EspressU

Thanks for helping. Pull requests are welcome.

## Licences of your contribution

- Most of the repository is under [MPL-2.0](LICENSE.txt). A contribution to those files is under MPL-2.0 too.
- Files EspressU wrote itself are under the [EspressU Licence](LICENSES/EspressU-Licence.txt) (non-commercial, fully open source, credit EspressU). Their header says so. These are `src/ios/App/ControllerMapping.swift`, `EspressUGamePacksView.swift`, `GameFilesController.swift`, `Settings/PerformanceTuning.swift`, `Settings/SessionReport.swift`, `Settings/LegacyLogCleanup.swift`, `src/ios/Bridge/IOSReport.cpp` and `IOSReport.h`. A contribution to one of them stays under that licence, with the same rights for EspressU as in the Crema Licence. Muffin and other third-party files keep their own licences.
- The touch controls in `src/ios/App/Crema/` are under the [Crema Licence](src/ios/App/Crema/LICENSE.txt). A contribution to a file in that folder stays under the Crema Licence. By sending it you confirm that you wrote it and may give it away, and you give EspressU the right to use, change, and relicense it, as described in section 7 of that licence.
- Do not copy code into the Crema folder from other projects, and do not copy Crema code into other projects unless you follow the Crema Licence (non-commercial, fully open source, credit EspressU).

## Before you send a pull request

- Keep changes small and focused, one topic per pull request.
- Never add keys, firmware, games, certificates, or passwords.
- Keep the licence headers of existing files in place. New files in the Crema folder use the Crema header from the other files there. New files written by EspressU use the EspressU Licence header from the files listed above.
