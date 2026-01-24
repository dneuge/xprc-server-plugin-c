# X-Plane Remote Control (Native Plugin written in C)

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
[![Issue Tracking: Codeberg](https://img.shields.io/badge/issue%20tracking-codeberg-2684cf)](https://codeberg.org/dneuge/xprc-server-plugin-c/issues)

XPRC provides an easy way to interact with X-Plane datarefs and commands from other applications/computers via a TCP network connection.

This project contains a native plugin implementing the protocol, written in C.

Official repositories are hosted on [Codeberg](https://codeberg.org/dneuge/xprc-server-plugin-c) and [GitHub](https://github.com/dneuge/xprc-server-plugin-c). Both locations are kept in sync and can be used to submit pull requests but issues are only tracked on [Codeberg](https://codeberg.org/dneuge/xprc-server-plugin-c/issues) to gather them in a single place. Please note that this project has a strict "no AI" policy [affecting all contributions](CONTRIBUTING.md) incl. issue reports.

Related projects:

- the [protocol specification](https://codeberg.org/dneuge/xprc-protocol) (available on [Codeberg](https://codeberg.org/dneuge/xprc-protocol) and [GitHub](https://github.com/dneuge/xprc-protocol))
- a [Java client library](https://codeberg.org/dneuge/xprc-client-java) (available on [Codeberg](https://codeberg.org/dneuge/xprc-client-java) and [GitHub](https://github.com/dneuge/xprc-client-java))

## Current State

In its current state the plugin is recommended only to be used as a **development preview**. Binary end-users releases will be published once the remaining blockers have been resolved:

- The plugin has some known stability issues which can deadlock X-Plane's main thread or may result in Crashes To Desktop (CTD). Those should be fixed before release to avoid user complaints.
- The protocol has some open todos preventing a "version 1.0" release. Most importantly it does not make sense to increment the general protocol revision, potentially prompting unnecessary client updates, each time new features are being added. Instead, clients should have a way to detect server feature availability. There are some ideas on how to do that but they have not been finalized and thus not implemented yet. Please refer to the specification project for details.
- Some originally intended security features are currently still missing on the network-side of this project.
- License information must be presented to users for binary releases. Documentation necessary to prepare that "paperwork" has been partially prepared by the [SBOM](sbom.xml) but any distribution still needs to provide a proper human-readable document/text file.

Work on this project started in February 2022 with the specification and a Python POC being written first. In 2025 the most critical stability issues were sufficiently mitigated for reliable use in a first application of limited scope. As of early 2026 it seemed unreasonable to hold back source code publication any longer, even if in its current state this project is only usable as a development preview. **A generally usable stable release is estimated to be published at some point in 2026** as it is also required for another project under active development.

If you need a production-ready way to control X-Plane *now* please consider using X-Plane's REST/WebSocket API in the mean-time.

## History

X-Plane offered a UDP network interface for a long time already but any more extensive control of the sim required writing a plugin (or script, e.g. using FlyWithLua or XPPython). The idea for XPRC came shortly after Hot Start released their excellent Challenger 650: Using PFPX for flight planning I could not find any performance profile, so I planned to write a tool to automatically perform the required test flights to derive the required data. This would have required to *remotely control the aircraft* from an external application (hence the name *X-Plane Remote Control*). As the UDP interface was not really suitable for that purpose, I gathered the required features and started work on a protocol specification and a (short-lived) proof-of-concept implementation.

Later in 2022 my focus shifted towards working on a bridge to another flight simulator, which required to also register DataRefs and X-Plane commands from XPRC instead of just interacting with existing ones. The expected high interaction rate also prompted to write a native plugin. Rust did not seem mature enough at that point, so C was chosen instead (a decision I started to regret a few years later).

In 2024, X-Plane 12.1.1 introduced a [REST/WebSockets API](https://developer.x-plane.com/article/x-plane-web-api/), solving most issues with the legacy UDP interface. The new API makes several of the original use-cases for XPRC at least partially obsolete. However, more advanced features such as registering custom DataRefs and X-Plane commands or "animating" DataRefs still is not supported through that new API.

## Development / Building

The project can be built for all platforms supported by X-Plane by simply running the included
scripts. Please refer to the [developer documentation](DEVELOPMENT.md) for details. 

An [SBOM](sbom.xml) is provided for detailed information about project dependencies.

## License

All sources and original files of this project are provided under [MIT license](LICENSE.md), unless declared otherwise
(e.g. by source code comments). Please be aware that dependencies (e.g. libraries and/or external data used by this
project) are subject to their own respective licenses which can affect distribution, particularly in binary/packaged
form. Patches included with this project, which are being applied to dependencies, fall under the same license as the
file/location being patched, while original content added by patches is released under MIT license as well
(dual-licensing), if applicable and not restricted by upstream licenses.

### Note on the use of/for AI

Usage for AI training is subject to individual source licenses, there is no exception. This generally means that proper
attribution must be given and disclaimers may need to be retained when reproducing relevant portions of training data.
When incorporating source code, AI models generally become derived projects. As such, they remain subject to the
requirements set out by individual licenses associated with the input used during training. When in doubt, all files
shall be regarded as proprietary until clarified.

Unless you can comply with the licenses of this project you obviously are not permitted to use it for your AI training
set. Although it may not be required by those licenses, you are additionally asked to make your AI model publicly
available under an open license and for free, to play fair and contribute back to the open community you take from.

AI tools are not permitted to be used for contributions to this project. The main reason is that, as of time of writing,
no tool/model offers traceability nor can today's AI models understand and reason about what they are actually doing.
Apart from potential copyright/license violations the quality of AI output is doubtful and generally requires more
effort to be reviewed and cleaned/fixed than actually contributing original work. Contributors will be asked to confirm
and permanently record compliance with these guidelines.

## Acknowledgements

X-Plane is a registered trademark of Austin Meyer and Aerosoft.
