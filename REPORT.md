# REPORT.md

### Q1: Difference between `stat()` and `lstat()`
The main difference is that `stat()` follows symbolic links and shows details of the file it points to, while `lstat()` gives details about the link itself. For `ls -l`, we use `lstat()` so that symbolic links are listed correctly instead of being replaced by the target file’s information.

---

### Q2: How `st_mode` stores type and permissions
The `st_mode` field stores both the type of file and its permissions in one value. Some bits show if it is a regular file, directory, or link, while other bits show read, write, and execute permissions for owner, group, and others. We use bitwise operators with macros like `S_IFDIR` or `S_IRUSR` to check these bits and display them properly.

## Feature 3: Column Display (Down Then Across)

**How printing in "down then across" works**  
To print items "down then across" we first collect all filenames and find the longest name. Knowing the terminal width and the longest name lets us compute how many columns fit. We then compute how many rows are required and print row-by-row: for each row we print the filename for column 0 (index = row), column 1 (index = row + rows), column 2 (index = row + 2*rows), and so on. A single loop over filenames cannot achieve this layout because you must map the linear list into a two-dimensional row/column grid where items in the same row are separated by a fixed column width.

**Why ioctl (TIOCGWINSZ) is used**  
The `ioctl` call with `TIOCGWINSZ` returns the current terminal width in columns. Using it allows the program to adapt the number of columns to the real terminal size so output fits nicely. If you always use a fixed width (like 80 columns), the layout will not adapt to larger or smaller terminals and can cause awkward wrapping or wasted space; detecting the real width yields a much closer match to the behavior of the standard `ls`.

