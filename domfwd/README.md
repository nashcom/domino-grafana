# domfwd - Domino Log Forwarder

The Domino Log Forwarder is a helper program which takes log data from STDIN

- Generates a Loki compatible JSON format and pushes log data to a [Loki API endpoint](https://grafana.com/docs/loki/latest/reference/loki-http-api/#ingest-logs)
- Annotates the log lines for ProcessID prefix using `pid.nbf` to provide the servertask information
- Writes the output data to STDOUT or a defined log file


## Flow

```
/opt/hcl/domino/bin/server | domfwd
```

## Environment Variables


The following variables are used to enable forwarding.

The following three output options exists and can be combined.
By default all log data is written to the Notes output log specified via **DOMINO_OUTPUT_LOG**.

In addition to the standard log file logs can be

- Pushed directly to Loki over HTTP API
- Written annotated to STOUT in JSON format ready for Alloy ingestion
- Written annotated to a file in JSON format ready for Alloy ingestion
- Mirrored to STDOUT for compatibility


### Output related configuration

The following table shows the main configuration.  
Not all output options should be combined.  
Please pick only one STDOUT option for example!


| Variable Name                | Description                       | Example / Comments                             |
|:-----------------------------|:----------------------------------|:-----------------------------------------------|
| **DOMINO_OUTPUT_LOG**        | Domino Output log file name       | /local/notesdata/notes.log                     |
| **LOKI_PUSH_API_URL**        | Push URL for Loki Server          | https://loki.example.com:3101/loki/api/v1/push |
| **LOKI_PUSH_TOKEN**          | Push Token for Loki Server        | my-secure-token                                |
| **DOMFWD_MIRROR_STDOUT**     | Mirror stdin to stdout            | 1                                              |
| **DOMFWD_ANNOTATE_STDOUT**   | Write JSON formatted Alloy STDOUT | 1                                              |
| **DOMFWD_ANNOATED_LOG**      | Write JSON formatted Alloy file   | /var/log/domfwd.json                           |


### Additional configuration

Most of the following parameters are optional.

| Variable Name                | Description                       | Example / Comments                             |
|:-----------------------------|:----------------------------------|:-----------------------------------------------|
| **LOKI_CA_FILE**             | Trusted Root CA File              | /local/notesdata/trusted_root.pem              |
| **LOKI_JOB**                 | Loki Job Name (default: hostname) |                                                |
| **DOMFWD_LOGLEVEL**          | Log level for stdout logging      | 1                                              |
| **DOMFWD_HOSTNAME**          | Hostname to use                   | default: hostname read from OS                 |
| **DOMFWD_PROM_FILE**         | Prom File for Metrics output      | default: <notesdata>/domino/stats/domfwd.prom  |



If no output log file is specified, log is written to STDOUT.
Letting **domfwd** write the log file avoids a redirect of the output log.


## Output log in JSON format for Loki API Push

- Log stream
- nano sec EPOCH time
- Labels passed via stream
- PID
- Annotated servertask name

```
{
  "streams": [
    {
      "stream": {
        "job": "mercury.domino.lab",
        "host": "mercury.domino.lab",
        "instance": "mercury.domino.lab",
        "namespace": "domino",
        "pod": "mercury.domino.lab",
        "pid": "86243",
        "process": "replica"
      },
      "values": [
        [
          "1770591063946850736",
          "[86243:000002-00007D9DC1973740] 01/31/2026 01:52:26   Finished replication with server pluto/NotesLab"
        ]
      ]
    }
  ]
}
```


## Output log in JSON format for Loki API Push


- Log stream
- nano sec EPOCH time
- PID
- Annotated servertask name


```
{"ts":1770749014360459684,"pid":86261,"process":"http","line":"[86261:000002-00007E61FEDAE2C0] 02/01/2026 00:48:03   HTTP Server: Shutdown"}

```

