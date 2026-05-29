This project uses internal GNU Bash implementation headers copied
from the Bash source tree into this folder.

Internal Bash APIs are not stable across releases.

When the bash version changes, the files in this folder should be updated and errors addressed.
The root reason why they are needed by the code should be considered and these dependencies
should be eliminated when possible.

# Updating Files

```
# This is where I keep the external bash code. It could be anywhere.
cd ~/github/bashUbuntu/
sudo apt install dpkg-dev
apt source bash
```

You can then diff the files in this folder with those in the bash source folder
and copy the new version into this folder.


# Reasons for needing thise files

* <fill in as discovered>
