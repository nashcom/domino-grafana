# How to install

This directory contains the Domino Linux64 servertask binary for domprom as a tar file.
The tar file name depends on the version you want to install.


Perform the following steps

- Extract the tar file (unless you built it on your own)
- Copy the binary
- Change file permission to make the binary executable
- Create a startup link

```
tar -xf domprom.taz
cp domprom /opt/hcl/domino/notes/latest/linux
chmod 755 /opt/hcl/domino/notes/latest/linux/domprom
cd /opt/hcl/domino/bin
ln -s tools/startup domprom
```
