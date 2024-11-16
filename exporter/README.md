# Domino Prometheus log exporter (domprom)

**domprom** is a small C-API based servertask, collecting all `LONG` and `NUMBER` statistics from the Domino server to present them in a file on disk with Prometheus compatible metrics names.

Note: Garfana and Prometheus can only handle numeric data. Text and Timedate data cannot be added. In future it could make sense to add **labels** to statistics. But that's not part of the first deliverable.

The resulting file can be used to either present them via HTML directory directly via HTTP as an end-point to a Prometheus server. Or include the data via [Prometheus Node Exporter for Linux](https://prometheus.io/docs/guides/node-exporter/).


## Compiling the servertask

The directory contains a the **makefile** for Linux and also a **mswin64.mak** file to compile on Windows.


### Linux command-line

```
make
```

The resulting file can be found in the main directory and need to be copied to the Domino binary directory.

To install the file on a Domino server use the following commands

```
cp domprom /opt/hcl/domino/notes/latest/linux
chmod 755 /opt/hcl/domino/notes/latest/linux/domprom
cd /opt/hcl/domino/bin
ln -s tools/startup domprom
```


### Windows command-line

```
nmake -f mswin64.mak
```

The resulting file can be found in the main directory and need to be copied to the Domino binary directory.


## Command line parameters

There are currently no command-line parameters required


## Domino Environment variables

All environment variables are optional. The default settings should be OK for most environments.

- **domprom_loglevel <n>** Log Level
- **domprom_outdir <dirname>** custom output directory (Default: **domino/stats/domino** in data directory)
- **domprom_outfile <filename>** custom output file name (Default: **domino/stats/domino.prom** in data directory)
- **domprom_interval <sec>** custom interval in seconds to update the statistic file (default: 30, min: 10)


## Windows/Linux Environment variables

- **DOMINO_PROM_STATS_DIR** custom directory for reading stats. Overwritten by Domino environment variables if specified


# Install and configure Node Exporter on Linux

Run the Node Exporter installation script `install_node_exporter.sh`.
It will download Node Exporter and install the service.

Ensure the configuration in systemd file is in sync with the Domino configuration.

```
ExecStart=/usr/bin/node_exporter --collector.textfile.directory=/local/notesdata/domino/stats
```
