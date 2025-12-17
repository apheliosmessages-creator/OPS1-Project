#!/bin/bash
set -x

# Clean up previous runs
# pkill mybackup_bin
rm -rf source target source2 target2 source_restored pipe backup.log
mkdir source

# Create initial files
echo "Hello World" > source/file1.txt
mkdir source/subdir
echo "Subfile" > source/subdir/subfile.txt

# Create pipe
mkfifo pipe

# Start backup program
./backup < pipe > backup.log 2>&1 &
BACKUP_PID=$!
exec 3>pipe

# Wait for startup
sleep 1

# Send add command
PWD=$(pwd)
echo "add $PWD/source $PWD/target" >&3
sleep 2

# Check if initial backup was successful
if [ -f target/file1.txt ] && [ -f target/subdir/subfile.txt ]; then
    echo "Initial backup: PASS"
else
    echo "Initial backup: FAIL"
    cat backup.log
fi

# Test Modification
echo "Modified" > source/file1.txt
sleep 2
if grep -q "Modified" target/file1.txt; then
    echo "Modification: PASS"
else
    echo "Modification: FAIL"
fi

# Test Creation
echo "New File" > source/newfile.txt
sleep 2
if [ -f target/newfile.txt ]; then
    echo "Creation: PASS"
else
    echo "Creation: FAIL"
fi

# Test Deletion
rm source/newfile.txt
sleep 2
if [ ! -f target/newfile.txt ]; then
    echo "Deletion: PASS"
else
    echo "Deletion: FAIL"
fi

# Test Subdirectory Creation
mkdir source/subdir2
sleep 1 # Allow watcher to be added
echo "Deep File" > source/subdir2/deep.txt
sleep 2
if [ -f target/subdir2/deep.txt ]; then
    echo "Subdir Creation: PASS"
else
    echo "Subdir Creation: FAIL"
fi

# Test Restore
mkdir source_restored
echo "restore $PWD/source_restored $PWD/target" >&3
sleep 2

if [ -f source_restored/file1.txt ] && [ -f source_restored/subdir/subfile.txt ]; then
    echo "Restore: PASS"
else
    echo "Restore: FAIL"
fi

# Test List
echo "list" >&3
sleep 1
if grep -q "Source:" backup.log; then
    echo "List: PASS"
else
    echo "List: FAIL"
    cat backup.log
fi

# Test End
echo "end $PWD/source $PWD/target" >&3
sleep 1
if grep -q "Backup ended" backup.log; then
    echo "End: PASS"
else
    echo "End: FAIL"
fi

# Cleanup
echo "exit" >&3
wait $BACKUP_PID
rm pipe
