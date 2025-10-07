# REPORT.md

### Q1: Difference between `stat()` and `lstat()`
The main difference is that `stat()` follows symbolic links and shows details of the file it points to, while `lstat()` gives details about the link itself. For `ls -l`, we use `lstat()` so that symbolic links are listed correctly instead of being replaced by the target file’s information.

---

### Q2: How `st_mode` stores type and permissions
The `st_mode` field stores both the type of file and its permissions in one value. Some bits show if it is a regular file, directory, or link, while other bits show read, write, and execute permissions for owner, group, and others. We use bitwise operators with macros like `S_IFDIR` or `S_IRUSR` to check these bits and display them properly.


