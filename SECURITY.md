# Security Policy

## Supported Versions

As long as the plugin is in "development preview" state (with no versions officially released for end-users), only the
latest trunk version (i.e. head revision of `main` Git branch) is supported.

## Attack Vectors

XPRC is a plugin for X-Plane, a flight simulator. The plugin listens on a user-customizable TCP port and requires a
randomly generated password to be entered during handshake. By default, only the IPv4 loopback interface is being bound,
however users may choose to bind to another interface incl. "all" and also opt-in to listen on IPv6. While users *can*
open the plugin to untrusted networks incl. public Internet connections this way, they are explicitly advised not to do
so. Users can easily return to safe defaults at the push of a button in the plugin's configuration dialog.

In the default "loopback" configuration, an attacker would need to already have at least partial control over the
computer being attacked. When users bind to a local IPv4 network, attackers may also intrude over the respective
network. By forwarding ports from the Internet to the bound interface or binding to a public interface (e.g. an
interface holding a public IPv6 address) users may expose the plugin to the public Internet.

As only trusted systems should be allowed access to the plugin (there is no valid use-case to make it directly
accessible over public Internet) network encryptions are unencrypted. To prevent users from exposing a reused password
in clear-text, the password required to pass the initial connection handshake cannot be configured manually through
regular means (i.e. the plugin settings dialog in X-Plane).

Being used to access/control a flight simulator, it would be unusual for any private personal information to be
communicated over the connection, so encryption is deemed unnecessary (data protection generally is no concern).

To allow local applications (on the same computer) to quickly connect to XPRC without any manual configuration, the
password is stored as plain-text in a well-known location within the X-Plane directory. By default, the password is
regenerated whenever the plugin gets reinitialized, although users may choose to hold on to the last generated password
to allow setting up connections from other systems.

### Known issues during developer preview

- Connection handshake is not yet properly secured during early developer preview:
  - hanging connections do not time out, allowing easy Denial of Service (exhausting the connection limit)
  - repeated unauthorized connection attempts are not blocked
- Failed commands/syntax errors may not abort the connection (practicality needs to be checked again; mentioned in
  specification as possible defense against incompatible/rogue clients, not just intentional attacks)

These issues will be worked on before the first end-user release is being made.

## Reporting Security Issues

Since the plugin is not supposed to be used for anything but a development preview in its current state, as of early
2026 you may decide to simply file a publicly visible
[issue on the official repository](https://codeberg.org/dneuge/xprc-server-plugin-c/issues).

Please check this file for future updates, as the process will change as the project gets closer to an end-user release.

From that point on, security issues should initially be reported privately to the project maintainer, allowing
reasonable time to confirm and start working on a resolution before publication to minimize impact on users
(Responsible Disclosure).

You can find an email address either in the Git history after cloning a local copy or on the website linked in the user
profile.

Additional ways to contact the project will be added once it is closer to a release aimed at end-users.

**Note that all reports made to the issue tracker are immediately visible to the public** (Full Disclosure).
