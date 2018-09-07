# About

This CheckedC version of parson is a fork of the parson JSON parsing library by kgabis (<https://github.com/kgabis/parson)>). The code is converted into CheckedC to provide bounds guarantees from the compiler (see <https://github.com/Microsoft/checked-c> for more information). DO **NOT** USE THIS CODE IN PRODUCTION. It is not kept up to date with the main parson project.

# Usage

Requires the checkedc-clang compiler (<https://github.com/Microsoft/checkedc-clang>) to build. If that is not aliased to `clang` on your system, edit the Makefile to set CC appropriately.

To build only: `make compile`

To build and then run the unit tests: `make` or `make test`

# Branches

baseline: The original C code that the conversion starts from. Note that this is already behind the main parson project. This is the version used in Azure's IOT C SDK as of July 2018. This branch should pass all 325 unit tests.

master: The converted CheckedC code.

# License

[The MIT License (MIT)](http://opensource.org/licenses/mit-license.php)

# Code of conduct

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).  For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.
