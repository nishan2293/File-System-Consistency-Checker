# fcheck: File System Consistency Checker

## Overview

fcheck is a tool developed to ensure the consistency of xv6 file system images. It reads a file system image and checks for various consistency rules. The program outputs specific error messages if inconsistencies are detected.

## Installation

Copy the xv6 source code from `/cs5348-xv6/src/xv6.tar.gz` to your local working directory. Extract the source code and run `make` to create the `fs.img`.

## Usage

Run fcheck with the following command format:
`prompt> fcheck <file_system_image>`


- `<file_system_image>`: Path to the file system image that needs to be checked.

### Error Messages

If fcheck detects inconsistencies, it outputs the specific error message and exits with error code 1. Examples of error messages include:

- `ERROR: bad inode.`
- `ERROR: bad direct address in inode.`
- `ERROR: root directory does not exist.`

and others as per the project specifications.

### No Image File

If no image file is provided, print the following to standard error and exit with error code 1:
`prompt> fcheck`

`Usage: fcheck <file_system_image>`


### Image Not Found

If the file system image does not exist, print `image not found.` to standard error and exit with error code 1.

### Successful Check

If no problems are detected, fcheck exits with a return code of 0 and does not output anything.

## Testing

Compile the program as follows:

`gcc fcheck.c -o fcheck -Wall -Werror -O`
