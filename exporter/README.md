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


# DomProm Main Business Time Configuration

DomProm supports configurable main business days and business hours to distinguish between production operating time and off-hours.

The configuration can be used to:

* Suppress alerts outside business hours
* Track uptime during main business time only
* Distinguish workdays from weekends
* Support operational dashboards
* Reduce alert noise during off-hours
* Support timezone-specific monitoring

The business time configuration is optional and disabled by default.


# Exported Statistics

## DominoHealth.business_day

Indicates whether the current day is as a business day.

| Value | Meaning          |
| ----- | ---------------- |
| 0     | Non-business day |
| 1     | Business day     |


## DominoHealth.business_hours

Indicates whether the current server time is within the configured main business hours.

| Value | Meaning |
|-------|---------|
| 0     | Outside business hours |
| 1     | Within business hours  |


Example:

```
DominoHealth.BusinessHours = 1
DominoHealth.BusinessDay = 1
```

# Configuration Variables

```
domprom_businessdays_enabled=1
domprom_businessdays=1-5
domprom_businesshours=6-18
domprom_businesshours5=6-13
domprom_businesshours_zone=-1
domprom_businesshours_dst=1
```

# Feature Enablement

The feature becomes enabled when:

* `domprom_businessdays` is configured
* OR `domprom_businessdays_enabled=1` is set

If the feature is disabled, DomProm always reports business hours as active.

# domprom_businessdays_enabled

Explicitly enables or disables the main business time feature.

| Value | Meaning                     |
| ----- | --------------------------- |
| 0     | Disable business time logic |
| 1     | Enable business time logic  |

Example:

```
domprom_businessdays_enabled=1
```

If enabled without defining business days, DomProm uses default business days and hours.

Default business days:

```
Monday-Friday
```

Default business hours:

```
06:00-18:00
```

# domprom_businessdays

Defines which weekdays are considered business days.
Business days use Unix-style weekday numbering and can range from:

| Day       | Value |
| --------- | ----- |
| Sunday    | 0     |
| Monday    | 1     |
| Tuesday   | 2     |
| Wednesday | 3     |
| Thursday  | 4     |
| Friday    | 5     |
| Saturday  | 6     |

Supported formats:

```
domprom_businessdays=1-5
domprom_businessdays=1,2,3,4,5
domprom_businessdays=1-5,0
domprom_businessdays=0,6
```

Ranges support wraparound:

```
domprom_businessdays=5-1
```

This example enables:

* Friday
* Saturday
* Sunday
* Monday

Invalid values outside the range `0-6` are rejected.


# domprom_businesshours

Defines the default main business hours used for all enabled business days.

Default:

```
domprom_businesshours=6-18
```

Supported formats:

```
6-18
06:00-18:00
06:00:00-18:00:00
```

The following formats are supported:

| Format            | Example             |
| ----------------- | ------------------- |
| Hours only        | `6-18`              |
| Hours and minutes | `06:00-18:00`       |
| Full timestamp    | `06:00:00-18:00:00` |


# Per-Day Business Hour Overrides

Specific weekdays can override the default business hours.

Format:

```
domprom_businesshours0=
domprom_businesshours1=
domprom_businesshours2=
domprom_businesshours3=
domprom_businesshours4=
domprom_businesshours5=
domprom_businesshours6=
```

Example:

```
domprom_businesshours=6-18
domprom_businesshours5=6-13
```

This configuration uses:

| Day             | Hours       |
| --------------- | ----------- |
| Monday-Thursday | 06:00-18:00 |
| Friday          | 06:00-13:00 |

# Overnight Business Hours

Business hours crossing midnight are supported.

Example:

```
domprom_businesshours=22-6
```

This range is active from:

* 22:00
* until 06:00 the next day


# Timezone Handling

## domprom_businesshours_zone

Overrides the Domino server timezone offset used for business time evaluation.

Example:

```
domprom_businesshours_zone=-1
```

The timezone is the Notes timezone. For example Germany uses -1 instead of GMT+1 to add the value (also negative to match GMT/UTC).
If not configured, the Domino server timezone is used.


## domprom_businesshours_dst

Overrides the Daylight Saving Time (DST) flag used for evaluation.


If not configured, the Domino server DST setting is used automatically.

# Feature Behavior Summary

| Configuration                         | Result                        |
| ------------------------------------- | ----------------------------- |
| No business time configuration        | Feature disabled              |
| `domprom_businessdays_enabled=1` only | Mon-Fri, 06:00-18:00          |
| `domprom_businessdays` configured     | Feature enabled automatically |
| Per-day business hours configured     | Override default hours        |
| Day not configured as business day    | Outside business time         |
| Overnight ranges                      | Supported                     |


Business days are internally normalized using Unix weekday numbering:

```
0 = Sunday
6 = Saturday
```
