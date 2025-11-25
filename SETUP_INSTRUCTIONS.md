# Setup Instructions for AOAlang Repository

Follow these steps to create the AOAlang repository on GitHub and push the initial code.

## Step 1: Create GitHub Repository

1. Go to https://github.com/new
2. Set repository details:
   - **Owner**: tiagoaoa
   - **Repository name**: `AOAlang`
   - **Description**: `Parser and validator for Arithmetic Optimization Algebra (AOA) - a language for zero-knowledge proof constraint systems`
   - **Visibility**: ✓ Private
   - **Initialize**: Do NOT initialize with README, .gitignore, or license (we have these already)
3. Click "Create repository"

## Step 2: Initialize and Push

Open a terminal in the AOAlang directory and run:

```bash
# Navigate to the project directory
cd /tmp/AOAlang

# Initialize git repository
git init

# Add all files
git add .

# Create initial commit
git commit -m "Initial commit: AOA parser with lex/yacc

- Full AOA grammar parser
- Semantic validation (declarations, types, indexing)
- Comprehensive error reporting
- POSIX-compatible build system
- Example .aoa files
- Complete documentation"

# Add remote (replace with your actual GitHub URL)
git remote add origin git@github.com:tiagoaoa/AOAlang.git

# Push to GitHub
git branch -M main
git push -u origin main
```

## Step 3: Verify Build

After pushing, verify the project builds:

```bash
./configure
make
```

Test with an example:

```bash
./bin/aoac examples/simple_quad.aoa
```

## Step 4: Set Repository Settings (Optional)

On GitHub (https://github.com/tiagoaoa/AOAlang/settings):

1. **About** (gear icon on main page):
   - Add topics: `parser`, `compiler`, `zero-knowledge`, `cryptography`, `lex`, `yacc`
   - Add website: Link to Zyga repo if desired

2. **Collaborators** (if needed):
   - Settings → Collaborators → Add people

## Step 5: Create GitHub Actions for CI (Optional)

Create `.github/workflows/build.yml`:

```yaml
name: Build and Test

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y flex bison gcc make
      - name: Configure
        run: ./configure
      - name: Build
        run: make
      - name: Test
        run: make test
```

## Troubleshooting

### SSH Key Issues

If you get permission denied with SSH:

```bash
# Use HTTPS instead
git remote set-url origin https://github.com/tiagoaoa/AOAlang.git
```

Or set up SSH keys: https://docs.github.com/en/authentication/connecting-to-github-with-ssh

### Build Errors

If configure fails:
```bash
# Ubuntu/Debian
sudo apt-get install build-essential flex bison

# macOS
brew install flex bison

# Fedora/RHEL
sudo dnf install gcc flex bison make
```

## Next Steps

1. Add CI/CD with GitHub Actions
2. Add more comprehensive tests
3. Create releases with pre-built binaries
4. Add integration with other tools (VS Code extension, pre-commit hooks, etc.)

## Contact

Issues? Email tiagoaoa@gmail.com
