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


| Variable Name                | Description                      | Example                                        |
|:-----------------------------|:---------------------------------|:-----------------------------------------------|
| **LOKI_PUSH_API_URL**        | Push URL for Loki Server         | https://loki.example.com:3101/loki/api/v1/push |
| **LOKI_PUSH_TOKEN**          | Push Token for Loki Server       | my-secure-token                                |
| **LOKI_CA_FILE**             | Trusted Root CA File             | /local/notesdata/trusted_root.pem              |
| **DOMINO_OUTPUT_LOG**        | Domino Output log file name      | /local/notesdata/notes.log                     |
| **DOMFWD_LOGLEVEL**          | Log level for stdout logging     | 1                                              |
| **DOMFWD_MIRROR_STDOUT**     | Mirror stdin to stdout           | 1                                              |
| **DOMFWD_ANNOTATE_STDOUT**   | Write annotated logs to stdout   | /local/notesdata/notes.log                     |



If no output log file is specified, log is written to STDOUT.
Letting **domfwd** write the log file avoids a redirect of the output log.


## Output log in JSON format

- Log stream
- Labels
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

