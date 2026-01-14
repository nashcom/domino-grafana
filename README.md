# domino-grafana

Domino Grafana & Loki

Grafana is a powerful widely used open source solution for visualizing statics and logs.  
This repository is intended to provide the basic functionality to collect statistics and logs from HCL Domino.

The current development focus is Domino on Linux including containers.  
But the servertask is also available on Windows and there is an exporter project for Windows as well.

## Garafana and Prometheus components

There are multiple components involved on the Grafana stack side in an integration.  
On first glance it looks like a lot of moving parts.  
But this project contains all the scripts and glue to make them work together in an easy to setup stack.

- [Grafana](https://grafana.com/oss/grafana/) itself is the visual representation and dashboarding component
- [Prometheus](https://grafana.com/oss/prometheus/) stores statistics data from servers providing a data source for Grafana
- [Prometheus Node Exporter for Linux](https://prometheus.io/docs/guides/node-exporter/) presents stats in a Prometheus compatible format to be collected by Prometheus on a defined endpoint
- [Windows Exporter](https://github.com/prometheus-community/windows_exporter) presents Windows stats and is a Prometheus community project
- [Grafana Loki](https://grafana.com/oss/loki/) stores log data from servers providing data source for Grafana
- [Promtail](https://grafana.com/docs/loki/latest/send-data/promtail/) is the data collecting component sending logs to Loki


# Quickstart/Installation & Configuration

The project contains simple setup steps for the individual components.
The first step should be always to clone the project.


```
mkdir -p /local/github
cd /local/github
git clone https://github.com/nashcom/domino-grafana.git
cd /local/github/domino-grafana
```


## domprom

The servertask "domprom" is the core component of this project.
It needs to be deployed on each Domino server.
By default when started the servertask writes it's .prom files into `<notesdata>/domino/stats`.


## node_exporter

The Prometheus Node Exporter collects OS level statistics and also the Domino .prom files written by `domprom`.
Depending on your environment the node_exporter runs:

- as part of the container image to only collect Domino statistics
- on your Linux host to collect OS statistics and Domino statistics


### Run as systemd service on the Linux host

For natively installed Domino servers and Docker/Podman hosts running Domino as a container,
the Node Exporter runs on host level to have full access OS level system statistics and to collect Domino statistics from **.prom** files.

There is currently no out of the box package with a [systemd](https://systemd.io/) script available in Linux distributions.
The binary is usually downloaded directly from the [Prometheus Node Exporter GitHub Page](https://github.com/prometheus/node_exporter).

This project contains a convenience script to

- Download and install the Node Exporter
- Configure a systemd service to run the Node Export
- Provide an out of the box configuration of the Node Exporter collecting Domino statistics from **.prom** files automatically.

The systemd setup script is part of the GitHub project and located here:

```exporter/install_node_exporter.sh```


### Run as part of the Domino container to collect Domino stats only

On Kubernetes (K8s) there is usually already an instance of Grafana and Prometheus involved collecting the OS statistics.
In that case the Node Exporter metrics end-point only provides Domino statistics.

The Domino container project provides a build option to install `domprom` and a current version of the Node Exporter automatically.

To start the Node Exporter just to to collect Domino statistics on Kubernetes specify the following environment parameter for your Domino container.

```
export NODE_EXPORTER_OPTIONS=default
```


### Port 9100/TCP inbound on Domino servers

Prometheus queries ("scrapes") the Metrics endpoint at `http://server:9100/metrics`.
Ensure that port 9100 is exposed (Docker port configuration and Linux firewall).

By default the port can be queried by any server or user who can connect to the server.
Usually this is not a problem, because only numeric statistic data is provided.
No PII should be included in the statistic data.

In case statistics should only be available to the collecting Prometheus servers, use a corporate or Linux firewall.


## Grafana and Prometheus

Prometheus is the base which collects ("scrapes") metrics endpoints which are served by the Node Exporter and stores them in it's internal time series database.
Grafana is the graphical interface which can get statistics data from multiple back-ends -- In our case Prometheus. 

You can use an existing instance of Prometheus to collect data and Grafana to display statistics.
If you don't have an existing infrastructure already, this project provides a Docker Compose stack to bring up all required components.

- Prometheus instance running internally on port 3030
- Grafana instance exposing port 3000 by default to allow hosting Grafana on an existing machine
- NGINX to provide TLS termination for the Grafana graphical interface

To bring up the compose stack you need a TLS/SSL certificate stored in the certs directory


## TLS Setup

NGINX is expecting the certificate and key in

- **/local/certs/cert.pem** certificate chain including root
- **/local/certs/key.pem** private key

Note: CertMgr can create exportable MicroCA certificates. It can be decrypted using an OpenSSL command line. It prompts for the password and stores it unencrypted.

```
openssl pkey -in encrypted_key -out key.pem
```


## Configure Domino servers to collect statistics from

When configuring a new instance copy `prometheus.yml.example` into `prometheus.yml` and specify the servers to **scrape**.
The example below also is helpful for your own Prometheus instance, where want to add those new targets to collect statistics from.

### Example configuration:

The following example defines two Domino servers to scrape in a single job.
The Node Exporter by default listens on port **9100**.

The `prometheus.yml` YAML based configuration is mounted into the Prometheus container to tell Prometheus which back-ends to scrape.

```
global:
  scrape_interval: 60s

scrape_configs:
  - job_name: domino
    static_configs:
      - targets:
          - notes.example.com:9100
        labels:
          DominoServer: notes.example.com

      - targets:
          - domino.example.com:9100
        labels:
          DominoServer: domino.example.com
```


## Bringing up the Docker Compose stack

Once those two configuration steps are provided, the compose stack can be started:

```
docker compose up -d
```

## First Login to Grafana

The default user and password is **admin/admin** and should be changed immediately.
Once the server is started, log into the portal. The GUI prompts to change the password.


## Grafana Provisioning Data Sources and Dashboards

Grafana provides a way to provision data sources and dashboards when first started.
This project provides the data source definition and also default dashboards, which you will find in the Example folder in the Grafana GUI.

They are updated over time and are intended as reference dashboards to copy into your own dashboards.
Over time we hopefully have reusable and published dashboards which can be imported via Grafana GUI.

The provisioning resources can be found in the `grafana/provisioning` folder.


## Grafana Dashbaord

The primary goal of this project is to provide the tooling to collect the metrics and logs from Domino.  
But of course it would make sense to also provide components to reuse in dashboards  
It would also make sense to provide a basic Domino dashboard in the official [Grafana Dashboard catalog](https://grafana.com/grafana/dashboards/)

The best starting point for a Domino on Linux dashboard would be begin with the [Node Exporter Full Dashboard](https://grafana.com/grafana/dashboards/1860-node-exporter-full/).
A good way is to copied the dashboard and added Domino statistics to it from the same data source.

The dashboard is also an excellent example to learn building dashboards.

To get the dashboard installed, enter the dashboard URL or the number of the dashboard which is **1860**

There is also a [Windows Node Exporter Dashboard](https://grafana.com/grafana/dashboards/14499-windows-node/) which is using the Windows Exporter in a similar way.


# Details about stats collection from Domino

## Challenge to export stats from Domino

In contrast to the existing [Domino Stats Publishing](https://help.hcltechsw.com/domino/14.0.0/admin/stats_publish_other_external.html) functionality based on a **push model** (initially introduced for New Relic integration in Domino 10)
Prometheus expects data being collected from a server using a **pull model**.

In addition the statistics names have to be in a specific [Prometheus format](https://prometheus.io/docs/concepts/data_model/).  
Basically **only alphanumeric chars** and the **underscore** can be used.


## Domino Prometheus log exporter (domprom)

This leads to a small add-on component available for Linux and Windows.  
**domprom** is a small C-API based servertask, collecting all LONG and NUMBER statistics from the Domino server to present them in a file on disk with Prometheus compatible metrics names.
Specially the dots in the names of Domino statistics need to be converted to underscores.

The exported stats are written to a file on disk, which can be included in an existing log collection.
The **Node Exporter** on Linux allows to include additional `.prom` files.
Those log files could be also presented via Domino HTTP directory on Windows or leveraging any other solution like [NGINX](https://www.nginx.com/).


## Setup metrics collection on Linux with node_exporter

[Prometheus Node Exporter for Linux](https://prometheus.io/docs/guides/node-exporter/) is a native for Linux OS level metrics.
It is a single binary available on [GitHub](https://github.com/prometheus/node_exporter), which should run on the host level to collect all relevant Linux statistics.

The **node_exporter** can be configured to send additional log files, which is the best choice for Domino metrics collection on Linux to ensure metrics are in-line between the OS and Domino.
The file generated by **domprom** can just be included and is send along with the Linux level stats.

See the setup and systemd scripts in [/exporter](/exporter/README.md) for details.


## Setup metrics collection on Windows with node_exporter

[Windows Exporter](https://github.com/prometheus-community/windows_exporter) is a native for Linux OS level metrics.
It is a single binary available on [GitHub](https://github.com/prometheus-community/windows_exporter/releases), which should run on the host level to collect all relevant Windows statistics.

The **Windows Exporter** can be configured to send additional log files, which is the best choice for Domino metrics collection on Windows to ensure metrics are in-line between the OS and Domino.


## Location of data

Data is stored in the following volumes.
In contrast all configuration are stored in native volumes.

- grafana-data
- loki-data
- prometheus-data


In earlier versions the data was located in native volumes, which was more complicated to setup.
When you migrate from an earlier version you have to move the data.



## Special notes for SELinux

When copying files the files might have the wrong security context even located in the right directory.
Relabeling the binary should help:

Example:

```
/usr/sbin/restorecon -v /usr/local/bin/node_exporter
```

The install script runs the relabeling if **restorecon** is installed.


## Setup log collection with Promtail for Loki

**promtail** is available for Linux and Windows to send logs to Loki over via push functionality.  
Prodmail is also a single binary available on [GitHub](https://github.com/grafana/loki).

See the setup and systemd scripts in [/exporter](/exporter/README.md) for details.




