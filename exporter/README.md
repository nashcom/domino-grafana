# Domino Prometheus log exporter (domprom)

**domprom** is a small C-API based servertask, collecting all LONG and NUMBER statistics from the Domino server to present them in a file on disk with Prometheus compatible metrics names.
The resulting file can be used to either present them via HTML directory directly via HTTP as an end-point to a Prometheus server. Or include the data via [Prometheus Node Exporter for Linux](https://prometheus.io/docs/guides/node-exporter/).


## Compiling the servertask

The directory contains a the **makefile** for Linux and also a **mswin64.mak** file to compile on Windows.


### Linux command-line

```
make
```

The resulting file can be found in the **bin/linux64** directory and need to be copied to the Domino binary directory.

### Windows command-line

```
make -f mswin64.mak
```

The resulting file can be found in the **bin/w64** directory and need to be copied to the Domino binary directory.


## Command line parameters

There are currently no command-line parameters required


## Environment variables

All environment variables are optional. The settings should be OK for most environments.

- **domprom_loglevel <n>** Log Level
- **domprom_outfile <filename>** custom output file name (Default: domino/domino.prom in data directory)
- **domprom_interval <sec>** custom interval in seconds to update the statistic file (default: 30, min: 10)
