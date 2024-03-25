#include <stdio.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>

#include "include/types.h"
#include "include/fs.h"

#define BLOCK_SIZE (BSIZE)

void exit_with_error(const char *error_message) {
    fprintf(stderr, "ERROR: %s\n", error_message);
    exit(1);
}

enum inode_types {
  INODE_FILE = 2,
  INODE_DIR = 1,  
  INODE_DEV = 3
};

typedef struct _img_pointers {
    char *mmapimage;
} img_pointers;

// Function to check if the bit at a given block address is set in the bitmap
bool is_bit_set(char *bitmapblocks, uint blockaddr) {
    char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
    return (bitmapblocks[blockaddr / 8] & bitarr[blockaddr % 8]) != 0;
}

// Check Point 1
// Function to validate the type of an inode
void validate_inode_type(struct dinode *inode) {
   bool is_invalid_inode_type = inode->type != INODE_FILE && inode->type != INODE_DIR && inode->type != INODE_DEV;

  // Check matched to allowed values
  if(is_invalid_inode_type) {
    exit_with_error("bad inode.");
  }

}

// Point 2
// Function to validate both direct and indirect block addresses in an inode
void validate_block_addresses(struct superblock *sb, struct dinode *inode, char *mmapimage) {
    // Validate direct block addresses
    for (int block_index = 0; block_index < NDIRECT; block_index++) {
        uint address = inode->addrs[block_index];
        if (address != 0 && (address < 0 || address >= sb->size)) {
            exit_with_error("bad direct address in inode.");
        }
    }

    // Validate indirect block addresses
    uint indirect_block_address = inode->addrs[NDIRECT];
    if (indirect_block_address == 0) return; // No indirect block

    if (indirect_block_address >= sb->size || indirect_block_address < 0) {
        exit_with_error("bad indirect address in inode.");
    }

    uint *indirect_block_ptr = (uint *)(mmapimage + indirect_block_address * BLOCK_SIZE);
    for (int idx = 0; idx < NINDIRECT; idx++, indirect_block_ptr++) {
        uint current_block_address = *indirect_block_ptr;
        if (current_block_address != 0 && (current_block_address >= sb->size || current_block_address < 0)) {
            exit_with_error("bad indirect address in inode.");
        }
    }
}

// Point 3: Verifies the existence of the root directory and that it references itself as its parent.
void check_root_directory(struct dinode *root_inode, char *mmapimage) {
    uint block_address = root_inode->addrs[0];
    if (block_address == 0) {
        exit_with_error("root directory does not exist.");
    }

    struct dirent *directory_entry = (struct dirent *)(mmapimage + block_address * BLOCK_SIZE);
    if (strcmp(directory_entry->name, ".") != 0 || directory_entry->inum != 1) {
        exit_with_error("root directory does not exist.");
    }
    directory_entry++;
    if (strcmp(directory_entry->name, "..") != 0 || directory_entry->inum != 1) {
        exit_with_error("root directory does not exist.");
    }
}

// Point 4: Ensures every directory has entries for '.' and '..', pointing to itself and its parent respectively.
void check_directory_entries(struct dinode *inode, char *mmapimage, int inode_number) {
    bool found_dot = false, found_dotdot = false;
    uint block_address;

    for (int dir_idx = 0; dir_idx < NDIRECT; dir_idx++) {
        block_address = inode->addrs[dir_idx];
        if (block_address == 0) continue;

        struct dirent *directory_entry = (struct dirent *)(mmapimage + block_address * BLOCK_SIZE);
        int entries_per_block = BSIZE / sizeof(struct dirent);
        for (int entry_idx = 0; entry_idx < entries_per_block; entry_idx++, directory_entry++) {
            if (strcmp(directory_entry->name, ".") == 0) {
                found_dot = true;
                if (directory_entry->inum != inode_number) {
                    exit_with_error("directory not properly formatted");
                }
            } else if (strcmp(directory_entry->name, "..") == 0) {
                found_dotdot = true;
            }
            if (found_dot && found_dotdot) break;
        }
        if (found_dot && found_dotdot) break;
    }

    if (!found_dot || !found_dotdot) {
        exit_with_error("directory not properly formatted.");
    }
}


// Function to verify directory structure integrity
// Point 3: Verifies the existence of the root directory and that it references itself as its parent.
// Point 4: Ensures every directory has entries for '.' and '..', pointing to itself and its parent respectively.
void validate_directory_structure(struct dinode *inode, char *mmapimage, int inode_number) {

    bool found_dot = false, found_dotdot = false;
    uint block_address;
    
    for (int dir_idx = 0; dir_idx < NDIRECT; dir_idx++) {
        block_address = inode->addrs[dir_idx];
        if (block_address == 0) continue;

        struct dirent *directory_entry = (struct dirent *)(mmapimage + block_address * BLOCK_SIZE);
        int entries_per_block = BSIZE / sizeof(struct dirent);
        for (int entry_idx = 0; entry_idx < entries_per_block; entry_idx++, directory_entry++) {
            if (strcmp(directory_entry->name, ".") == 0) {
                found_dot = true;
                if (directory_entry->inum != inode_number) {
                    exit_with_error("directory not properly formatted");
                }
            } else if (strcmp(directory_entry->name, "..") == 0) {
                found_dotdot = true;
                bool root_dir_issue = (inode_number == 1 && directory_entry->inum != inode_number) ||
                                      (inode_number != 1 && directory_entry->inum == inode_number);
                if (root_dir_issue) {
                    exit_with_error("root directory does not exist.");
                }
            }
            if (found_dot && found_dotdot) break;
        }
        if (found_dot && found_dotdot) break;
    }

    if (!found_dot || !found_dotdot) {
        exit_with_error("directory not properly formatted.");
    }
}

// Point 5
// Function to ensure that all addresses used by an inode are marked as used in the bitmap
void validate_bitmap_addr(char *bitmapblocks, struct dinode *inode, char *mmapimage) {
    for (int idx = 0; idx <= NDIRECT; idx++) {
        uint address = inode->addrs[idx];
        if (address != 0 && !is_bit_set(bitmapblocks, address)) {
            exit_with_error("address used by inode but marked free in bitmap.");
        }

        // Special handling for indirect address
        if (idx == NDIRECT) {
            uint *indirect_block = (uint *)(mmapimage + address * BLOCK_SIZE);
            for (int indirect_idx = 0; indirect_idx < NINDIRECT; indirect_idx++) {
                uint indirect_address = indirect_block[indirect_idx];
                if (indirect_address != 0 && !is_bit_set(bitmapblocks, indirect_address)) {
                    exit_with_error("address used by inode but marked free in bitmap.");
                }
            }
        }
    }
}

// This function performs a series of checks on each inode as per the specified points 1 to 5.
// It iterates through each inode to ensure they adhere to the defined filesystem integrity points.
void validate_inodes(char *inodeblocks, char *bitmapblocks, char *mmapimage, struct superblock *sb) {
    struct dinode *current_inode = (struct dinode *)inodeblocks;

    for (int inode_index = 0; inode_index < sb->ninodes; inode_index++, current_inode++) {
        if (current_inode->type == 0) {
            // Skip processing for unallocated (free) inodes
            continue;
        }

        // Point 1: Validate inode type
        validate_inode_type(current_inode);

        // Point 2: Validate direct and indirect block addresses
        validate_block_addresses(sb, current_inode, mmapimage);

        // Point 3 and 4: Validate directory structure
        if (inode_index == 1) { // Root directory specific check
            if (current_inode->type != INODE_DIR) {
                exit_with_error("root directory does not exist.");
            }
            validate_directory_structure(current_inode, mmapimage, 1);
        } else if (current_inode->type == INODE_DIR) {
            validate_directory_structure(current_inode, mmapimage, inode_index);
        }

        // Point 5: Validate bitmap address
        validate_bitmap_addr(bitmapblocks, current_inode, mmapimage);
    }
}


// Point 6
// This function identifies the data blocks actively used by a given inode.
// It marks both direct and indirect blocks as used in the used_dbs array.
void get_active_data_blocks(struct dinode *inode, int *used_blocks_array, char *mmapimage, uint starting_block){
    uint address;
    
    // Looping through each address in the inode
    for (int addr_idx = 0; addr_idx <= NDIRECT; addr_idx++) {
        address = inode->addrs[addr_idx];
        if (address == 0) continue; // Skipping empty addresses

        // Marking the block as used
        used_blocks_array[address - starting_block] = 1;

        // Processing indirect blocks
        if (addr_idx == NDIRECT) {
            uint *indirect_block = (uint *)(mmapimage + address * BLOCK_SIZE);
            for (int indirect_idx = 0; indirect_idx < NINDIRECT; indirect_idx++) {
                uint indirect_address = indirect_block[indirect_idx];
                if (indirect_address != 0) {
                    used_blocks_array[indirect_address - starting_block] = 1;
                }
            }
        }
    }
}

// Point 6
// Validates that all blocks marked as used in the bitmap are indeed used by some inode.
void verify_bitmap_usage(char *inodeblocks, char *bitmapblocks, char *mmapimage, struct superblock *sb, uint start_block) {
    struct dinode *current_inode = (struct dinode *)inodeblocks;
    int blocks_in_use[sb->nblocks];
    memset(blocks_in_use, 0, sizeof(int) * sb->nblocks);

    // Iterating through inodes to flag used blocks
    for (int inode_idx = 0; inode_idx < sb->ninodes; inode_idx++, current_inode++) {
        if (current_inode->type == 0) continue; // Skip unused inodes

        get_active_data_blocks(current_inode, blocks_in_use, mmapimage, start_block);
    }

    // Verifying bitmap against the block usage array
    for (int block_idx = 0; block_idx < sb->nblocks; block_idx++) {
        uint actual_block_addr = block_idx + start_block;
        if (blocks_in_use[block_idx] == 0 && is_bit_set(bitmapblocks, actual_block_addr)) {
            exit_with_error("bitmap marks block in use but it is not in use.");
        }
    }
}

// Point 7
// This function tallies the occurrences of direct block addresses used by an inode.
void tally_direct_block_usage(struct dinode *target_inode, uint *direct_usage_array, uint start_block) {
    uint address;

    // Looping through the direct addresses in the inode
    for (int idx = 0; idx < NDIRECT; idx++) {
        address = target_inode->addrs[idx];
        if (address == 0) continue; // Skip if the address is not in use

        // Incrementing the usage count for each direct block address
        direct_usage_array[address - start_block]++;
    }
}

// Point 8 
// This function accounts for the usage of indirect block addresses within an inode.
void count_indirect_block_usage(struct dinode *target_inode, uint *indirect_usage_array, char *fs_image, uint start_block) {
    uint indirect_block_address = target_inode->addrs[NDIRECT];

    if (indirect_block_address == 0) return; // No indirect block present

    // Accessing the array of indirect block addresses
    uint *indirect_block_ptr = (uint *)(fs_image + indirect_block_address * BLOCK_SIZE);

    // Iterating over the indirect block addresses
    for (int idx = 0; idx < NINDIRECT; idx++) {
        uint current_address = indirect_block_ptr[idx];
        if (current_address != 0) {
            // Incrementing the usage count for each indirect block address
            indirect_usage_array[current_address - start_block]++;
        }
    }
}

// Point 7 and 8
// Function to ensure that each block address within in-use inodes is uniquely used.
void validate_block_address_uniqueness(char *inodeblocks, char *fs_image, struct superblock *sb, uint start_block) {
    struct dinode *current_inode = (struct dinode *)inodeblocks;
    
    // Arrays to track the usage count of direct and indirect addresses.
    uint direct_usage_counts[sb->nblocks];
    uint indirect_usage_counts[sb->nblocks];
    memset(direct_usage_counts, 0, sizeof(uint) * sb->nblocks);
    memset(indirect_usage_counts, 0, sizeof(uint) * sb->nblocks);

    // Iterating through each inode to accumulate address usage
    for (int inode_idx = 0; inode_idx < sb->ninodes; inode_idx++, current_inode++) {
        if (current_inode->type == 0) continue; // Skip unused inodes

        // Tallying the usage of direct and indirect block addresses
        tally_direct_block_usage(current_inode, direct_usage_counts, start_block);
        count_indirect_block_usage(current_inode, indirect_usage_counts, fs_image, start_block);
    }

    // Verifying that each address is used only once
    for (int block_idx = 0; block_idx < sb->nblocks; block_idx++) {
        if (direct_usage_counts[block_idx] > 1) {
            exit_with_error("direct address used more than once.");
        }
        if (indirect_usage_counts[block_idx] > 1) {
            exit_with_error("indirect address used more than once.");
        }
    }
}


//function for point 9, 10, 11, 12
//iterate through all directories and count for inodemap (how many times each inode number has been refered by directory).
void scan_directory_entries(char *inodeblocks, img_pointers *image, struct dinode *rootinode, int *inodemap) {
    int initialSize = 100; // Initial stack size, adjust as needed
    struct dinode **stack = malloc(initialSize * sizeof(struct dinode *));

    int stackSize = initialSize; 
    int stackTop = 0;

    // Push the root directory onto the stack
    stack[stackTop++] = rootinode;

    while (stackTop > 0) {
        struct dinode *current = stack[--stackTop];

        // Skip if not a directory
        if (current->type != INODE_DIR) {
            continue;
        }

        // Traverse direct addresses
        for (int i = 0; i < NDIRECT; i++) {
            uint blockaddr = current->addrs[i];
            if (blockaddr == 0) continue;

            struct dirent *dir = (struct dirent *)(image->mmapimage + blockaddr * BLOCK_SIZE);
            int entries_per_block = BSIZE / sizeof(struct dirent);

            for (int j = 0; j < entries_per_block; j++, dir++) {
                if (dir->inum != 0 && strcmp(dir->name, ".") != 0 && strcmp(dir->name, "..") != 0) {
                    inodemap[dir->inum]++;
                    struct dinode *next = ((struct dinode *)(inodeblocks)) + dir->inum;
                    if (stackTop == stackSize) {
                        // Resize the stack if needed
                        stackSize *= 2;
                        stack = realloc(stack, stackSize * sizeof(struct dinode *));
                    }
                    stack[stackTop++] = next;
                }
            }
        }

        // Traverse indirect address
        uint blockaddr = current->addrs[NDIRECT];

        if (blockaddr != 0) {
            uint *indirect = (uint *)(image->mmapimage + blockaddr * BLOCK_SIZE);
            for (int i = 0; i < NINDIRECT; i++, indirect++) {
                blockaddr = *indirect;
                if (blockaddr == 0) continue;

                struct dirent *dir = (struct dirent *)(image->mmapimage + blockaddr * BLOCK_SIZE);
                int entries_per_block = BSIZE / sizeof(struct dirent);
                for (int j = 0; j < entries_per_block; j++, dir++) {
                    if (dir->inum != 0 && strcmp(dir->name, ".") != 0 && strcmp(dir->name, "..") != 0) {
                        inodemap[dir->inum]++;
                        struct dinode *next = ((struct dinode *)(inodeblocks)) + dir->inum;
                        if (stackTop == stackSize) {
                            // Resize the stack if needed
                            stackSize *= 2;
                            stack = realloc(stack, stackSize * sizeof(struct dinode *));
                        }
                        stack[stackTop++] = next;
                    }
                }
            }
        }
    }

    free(stack);
}

// Point 9, 10, 11, 12
// Validates directory-related points across all in-use inodes.
void validate_directory_rules(char *inode_blocks, img_pointers *img, struct superblock *fs_sb) {
    int inode_references[fs_sb->ninodes];
    memset(inode_references, 0, sizeof(int) * fs_sb->ninodes);
    struct dinode *curr_inode;
    struct dinode *root_inode;
    curr_inode = (struct dinode *)inode_blocks;
    root_inode=++curr_inode;
    
    // Increment reference count for reserved inodes
    inode_references[0]++;
    inode_references[1]++;

    // Analyzing directory entries to count inode references
    scan_directory_entries(inode_blocks, img, root_inode, inode_references); // Skips the first inode (root)
    
    // Verifying directory points for each inode
    curr_inode++;
    for (int inode_idx = 2; inode_idx < fs_sb->ninodes; inode_idx++, curr_inode++) {
        if (curr_inode->type != 0 && inode_references[inode_idx] == 0) {
            exit_with_error("inode marked use but not found in a directory.");
        }
        if (inode_references[inode_idx] > 0 && curr_inode->type == 0 ) {
            exit_with_error("inode referred to in directory but marked free.");
        }
        if (curr_inode->type == INODE_FILE && curr_inode->nlink != inode_references[inode_idx]) {
            exit_with_error("bad reference count for file.");
        }
        if (curr_inode->type == INODE_DIR && inode_references[inode_idx] > 1) {
            exit_with_error("directory appears more than once in file system.");
        }
    }
}


int main(int argc, char *argv[]) {
    int fsfd;
    struct stat fileStat;
    img_pointers image;
    char *mmapimage;
    struct superblock *sb;
    
    uint numinodeblocks, startingblock;
    char *inodeblocks, *bitmapblocks;

    if (argc < 2) {
        fprintf(stderr, "Usage: fcheck <file_system_image>\n");
        exit(1);
    }

    fsfd = open(argv[1], O_RDONLY);
    if (fsfd < 0) {
        fprintf(stderr, "image not found\n");
        exit(1);
    }

    if (fstat(fsfd, &fileStat) < 0) {
        exit(1);
    }

    mmapimage = mmap(NULL, fileStat.st_size, PROT_READ, MAP_PRIVATE, fsfd, 0);
    if (mmapimage == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    image.mmapimage=mmapimage;
    sb = (struct superblock *)(mmapimage + 1 * BLOCK_SIZE);
    numinodeblocks = (sb->ninodes / (IPB)) + 1;
 
    inodeblocks = (char *)(mmapimage + 2 * BLOCK_SIZE);
    
    bitmapblocks = (char *)(inodeblocks + numinodeblocks * BLOCK_SIZE);
    startingblock = ((sb->ninodes/(IPB))+1)+((sb->size/(BPB))+1)+2;

    validate_inodes(inodeblocks, bitmapblocks, mmapimage, sb);
    verify_bitmap_usage(inodeblocks, bitmapblocks, mmapimage, sb, startingblock);
    validate_block_address_uniqueness(inodeblocks, mmapimage, sb, startingblock);
    validate_directory_rules(inodeblocks, &image, sb);

    exit(0);
}
