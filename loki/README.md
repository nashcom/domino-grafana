
# Grafana Loki

Grafana Loki support is currently under development/experimental.
The main focus is currently statistics monitoring.

There has not been any request about Loki and collecting Logs is a different animal than statistics.

It is a very interesting project we are going to look into next.
The resources have been moved from the exporter directory to this new directory to separate it from the Node Exporter implementation.

## Setup log collection with Promtail for Loki

**promtail** is available for Linux and Windows to send logs to Loki over via push functionality.  
Prodmail is also a single binary available on [GitHub](https://github.com/grafana/loki).


## How to setup

Loki is an optional container in the compose stack.
On the collection side it needs [promtail](https://grafana.com/docs/loki/latest/send-data/promtail/)
another open source component with a separate download installed in a similar way the Prometheus Node Exporter is installed.

## Note

There is currently no Domino container integration.
If you are interested in Loki open an issue in this project and we can have a look.
The main focus for now is statistics collection.

The resources are parked here for further project steps.

