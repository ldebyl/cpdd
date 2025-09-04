# cpdd - Content-based Copy with Deduplication

A high-performance file copying utility that uses content-based deduplication with reference directories to save space through hard or symbolic linking.

## Features

- **Three-tier matching**: Size → MD5 → byte-by-byte comparison for accuracy
- **Multiple reference directories**: Use `-r` multiple times  
- **Hard and symbolic linking**: Save space by linking to existing identical files
- **Recursive directory copying**: Full directory tree support
- **Progress reporting**: Multiple verbosity levels with human-readable output
- **Cross-platform**: Linux, macOS, and BSD support

## Quick Start

```bash
# Copy with deduplication using reference directory
./cpdd -r /backup/reference -R /source /destination

# Create hard links to matching files  
./cpdd -r /reference -L file.txt /dest/

# Create symbolic links with verbose output
./cpdd -r /reference -s -v /source /dest
```

## Installation

### From Release
```bash
# Download and extract for your platform
tar xzf cpdd-v1.0.0-linux-x86_64.tar.gz
sudo cp linux-x86_64/cpdd linux-x86_64/syndir /usr/local/bin/
sudo cp linux-x86_64/man/*.1 /usr/local/share/man/man1/
```

### From Source
```bash
make all
sudo make install
```

## Usage

```
cpdd [OPTIONS] SOURCE... DESTINATION

Options:
  -r, --reference DIR    Reference directory for deduplication  
  -L, --hard-link       Create hard links (default with -r)
  -s, --symbolic-link   Create symbolic links  
  -R, --recursive       Copy directories recursively
  --stats               Show operation statistics
  -v, --verbose         Increase verbosity (-vv, -vvv)
  --help                Show help
```

## Building

```bash
make all          # Build binaries
make test         # Run test suite  
make static       # Build static binaries
make debug        # Debug build with AddressSanitizer
```

## License

MIT License - see source files for details.

## Author

Lee de Byl <lee@32kb.net>