# cpdd {{VERSION}}

Content-based copy with deduplication - Binary releases for multiple platforms.

## Downloads

Choose the appropriate binary for your platform:

- **Linux x86_64**: `cpdd-*-linux-x86_64.tar.gz`
- **macOS x86_64**: `cpdd-*-macos-x86_64.tar.gz`
- **macOS ARM64**: `cpdd-*-macos-arm64.tar.gz`
- **FreeBSD x86_64**: `cpdd-*-freebsd-x86_64.tar.gz`
- **FreeBSD aarch64**: `cpdd-*-freebsd-aarch64.tar.gz`
- **OpenBSD x86_64**: `cpdd-*-openbsd-x86_64.tar.gz`
- **OpenBSD aarch64**: `cpdd-*-openbsd-aarch64.tar.gz`
- **Solaris x86_64**: `cpdd-*-solaris-x86_64.tar.gz`
- **Solaris SPARC**: `cpdd-*-solaris-sparc.tar.gz`

## Quick Install

```bash
# Extract archive
tar -xzf cpdd-*-your-platform.tar.gz
cd cpdd-*/

# Run install script
sudo ./install.sh

# Or install to custom location
PREFIX=$HOME/.local ./install.sh
```

## Manual Installation

1. Download the appropriate archive for your platform
2. Extract: `tar -xzf cpdd-*.tar.gz`
3. Copy binaries to your PATH: `sudo cp cpdd-*/cpdd cpdd-*/syndir /usr/local/bin/`
4. Install man pages: `sudo cp cpdd-*/man/*.1 /usr/local/share/man/man1/`

## Verification

Each archive includes a SHA256 checksum file. Verify with:
```bash
sha256sum -c cpdd-*.tar.gz.sha256
```

See the commit history for changes in this release.