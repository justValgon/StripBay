# AI Disclosure

🇬🇧 English · 🇩🇪 [Deutsch](AI_DISCLOSURE.de.md)

## Statement

StripBay was developed with **heavy use of artificial intelligence**. The large majority of the source code, the build scripts, the tests and the documentation were written by the AI assistant **Claude** (Anthropic) in a continuous dialogue with the authors, Luis Korb and Sören Pflug.

## Division of work

- **Authors:** requirements, design decisions, feature requests, priorities, review of results, publication and responsibility for this repository.
- **AI (Claude):** drafting and modifying C++ code, scripts and workflows, writing and running automated tests, preparing documentation and preview images, translating texts.

The authors decide what is published. The AI does not act independently of them.

## Verification

- Automated tests run on every change that is prepared with the AI: unit tests, a multi-controller simulation and randomised invariant checks (with AddressSanitizer and UBSan). These run in a Linux environment with a substitute for the Windows and EuroScope interfaces.
- The Windows DLL is built by GitHub Actions (MSBuild, Release/x86).
- The tests **cannot** replace use in a real EuroScope session. Behaviour with real traffic, other plug-ins (TopSky, GRP, vACDM, vSID, Hoppie) and different EuroScope versions can differ.

## Limitations

- The software is provided **without warranty** (see [LICENSE](LICENSE)). Errors, including incorrect strip states or handover states, are possible.
- AI-generated code may contain mistakes that are not obvious. Please report problems as an [issue](../../issues/new/choose).
- StripBay is intended for **flight simulation** (for example on online networks). It is **not** a certified system and must **not** be used for real air traffic control or any operational aviation purpose.
- The two files in `StripBay/sdk/` come from the EuroScope SDK and were not created by the authors or by AI.

## Licence

The code is published under the [MIT licence](LICENSE). The use of AI does not change the licence terms.
