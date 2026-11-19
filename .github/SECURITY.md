# Security policy

remotewin carries windows from a Windows guest to the Linux host. The guest half is
untrusted: whatever it sends, the host half must only ever draw it in that
application's own windows. A way around that is a vulnerability: a guest that can
crash the host half through a malformed message, read host memory, draw outside its
windows, or inject input into other host applications.

## Reporting a vulnerability

Please report vulnerabilities privately, through
[a security advisory](https://github.com/acceleration3/remotewin/security/advisories/new),
not in a public issue. Include the commit, the compositor, and the message
sequence or steps that show it.

Once a fix is released, the advisory is published with credit to you, unless you
would rather stay anonymous.

## Supported versions

Only `main` gets security fixes.
