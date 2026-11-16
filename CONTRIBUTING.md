# Contributing to EspressU

Thanks for helping. Pull requests are welcome.

## Before you send a pull request

- Keep changes small and focused, one topic per pull request.
- Never add keys, firmware, games, certificates or passwords.
- Keep the licence headers of existing files in place.
- Put the right header on new files (see below).

## Licences of your contribution

Which licence your contribution falls under depends on the file you change.

| Where | Licence | What it means for your contribution |
| --- | --- | --- |
| Most of the repository | [MPL-2.0](LICENSE.txt) | Your contribution is under MPL-2.0 too. |
| Files written by EspressU (the header says so) | [EspressU Licence](LICENSES/EspressU-Licence.txt) | Non-commercial, fully open source, credit EspressU. Your contribution stays under that licence, with the same rights for EspressU as in the Crema Licence. |
| `src/ios/App/Crema/` (touch controls) | [Crema Licence](src/ios/App/Crema/LICENSE.txt) | Your contribution stays under the Crema Licence. By sending it you confirm that you wrote it and may give it away, and you give EspressU the right to use, change and relicense it, as described in section 7 of that licence. |

Muffin and other third-party files keep their own licences.

## New files

- **In `src/ios/App/Crema/`:** use the Crema header from the other files in that folder.
- **Other new files written by EspressU:** use the EspressU Licence header from an existing EspressU file.

## Code from other projects

- Do not copy code into the Crema folder from other projects.
- Do not copy Crema code into other projects unless you follow the Crema Licence: non-commercial, fully open source, credit EspressU.
