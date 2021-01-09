# Introduction

**RUNW** is a OCI compatible runtime for running WASI enabled WebAssembly file inside a container envrionment.

# Getting Started

## Prerequisite
* [cri-o](https://cri-o.io/)
* [crictl](https://github.com/kubernetes-sigs/cri-tools)
* Optional [buildah](https://github.com/containers/buildah) or [docker](https://github.com/docker/cli) for building container image

## Get Source Code

```bash
$ git clone git@github.com:second-state/RUNW.git
$ cd RUNW
$ git checkout 0.0.1
```

## Prepare the environment

### Use our docker image

Our docker image use `ubuntu 20.04` as the base.

```bash
$ docker pull secondstate/ssvm
```

### Or setup the environment manually

```bash
# Tools and libraries
$ sudo apt install -y \
        software-properties-common \
        cmake \
        libboost-all-dev

# And you will need to install llvm for ssvmc tool
$ sudo apt install -y \
        llvm-dev \
        liblld-10-dev

# SSVM supports both clang++ and g++ compilers
# You can choose one of them for building this project
$ sudo apt install -y gcc g++
$ sudo apt install -y clang
```

## Build RUNW

```bash
# After pulling our ssvm docker image
$ docker run -it --rm \
    -v <path/to/your/runw/source/folder>:/root/runw \
    secondstate/ssvm:latest
(docker)$ cd /root/runw
(docker)$ mkdir -p build && cd build
(docker)$ cmake -DCMAKE_BUILD_TYPE=Release .. && make -j
(docker)$ exit
```

## Install runw into cri-o

```bash
sudo cp -v build/src/runw /usr/lib/cri-o-runc/sbin/runw
sudo sed -i -e 's@default_runtime = "runc"@default_runtime = "runw"@' /etc/crio/crio.conf
sudo sed -i -e 's@pause_image = "k8s.gcr.io/pause:3.2"@pause_image = "docker.io/beststeve/wasm-pause"@' /etc/crio/crio.conf
sudo sed -i -e 's@pause_command = "/pause"@pause_command = "pause.wasm"@' /etc/crio/crio.conf
sudo tee -a /etc/crio/crio.conf.d/01-crio-runc.conf <<EOF
[crio.runtime.runtimes.runw]
runtime_path = "/usr/lib/cri-o-runc/sbin/runw"
runtime_type = "oci"
runtime_root = "/run/runw"
EOF
```

## Restart cri-o

```bash
sudo systemctl restart crio
```

## Create container image

This example uses [buildah](https://github.com/containers/buildah) to build image. You can use any other tools to create container image.

```bash
$ tee Dockerfile <<EOF
FROM scratch
ADD nbody-c.wasm .
CMD ["nbody-c.wasm"]
EOF
$ buildah bud -f Dockerfile -t wasm-nbody-c
$ buildah push wasm-nbody-c docker://registry.example.com/repository:tag
```

## Create container config

```bash
$ tee container_nbody.json <<EOF
{
  "metadata": {
    "name": "podsandbox1-wasm-nbody"
  },
  "image": {
    "image": "beststeve/wasm-nbody-c:latest"
  },
  "args": [
    "nbody-c.wasm", "50000000"
  ],
  "working_dir": "/",
  "envs": [],
  "labels": {
    "tier": "backend"
  },
  "annotations": {
    "pod": "podsandbox1"
  },
  "log_path": "",
  "stdin": false,
  "stdin_once": false,
  "tty": false,
  "linux": {
    "resources": {
      "memory_limit_in_bytes": 209715200,
      "cpu_period": 10000,
      "cpu_quota": 20000,
      "cpu_shares": 512,
      "oom_score_adj": 30,
      "cpuset_cpus": "0",
      "cpuset_mems": "0"
    },
    "security_context": {
      "namespace_options": {
        "pid": 1
      },
      "readonly_rootfs": false,
      "capabilities": {
        "add_capabilities": [
          "sys_admin"
        ]
      }
    }
  }
}
EOF
```

## Create cri-o POD
```bash
$ tee sandbox_config.json <<EOF
{
  "metadata": {
    "name": "podsandbox1",
    "uid": "redhat-test-crio",
    "namespace": "redhat.test.crio",
    "attempt": 1
  },
  "hostname": "crictl_host",
  "log_directory": "",
  "dns_config": {
    "searches": [
      "8.8.8.8"
    ]
  },
  "port_mappings": [],
  "resources": {
    "cpu": {
      "limits": 3,
      "requests": 2
    },
    "memory": {
      "limits": 50000000,
      "requests": 2000000
    }
  },
  "labels": {
    "group": "test"
  },
  "annotations": {
    "owner": "hmeng",
    "security.alpha.kubernetes.io/seccomp/pod": "unconfined"
  },
  "linux": {
    "cgroup_parent": "pod_123-456.slice",
    "security_context": {
      "namespace_options": {
        "network": 0,
        "pid": 1,
        "ipc": 0
      },
      "selinux_options": {
        "user": "system_u",
        "role": "system_r",
        "type": "svirt_lxc_net_t",
        "level": "s0:c4,c5"
      }
    }
  }
}
EOF
$ # Create the POD. Output will be different from example.
$ crictl runp sandbox_config.json
7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
$ # Set a helper variable for later use.
$ POD_ID=7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
```

## Create Container
```bash
$ crictl create $POD_ID container_nbody.json sandbox_config.json
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
```

## Start Container
```bash
$ crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       beststeve/wasm-nbody-c:latest   About a minute ago   Created             podsandbox1-wasm-nbody   0                   7992e75df00cc
$ crictl start 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
$ crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       beststeve/wasm-nbody-c:latest   About a minute ago   Running             podsandbox1-wasm-nbody   0                   7992e75df00cc
$ # Wait until container finished
$ crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       beststeve/wasm-nbody-c:latest   About a minute ago   Exited              podsandbox1-wasm-nbody   0                   7992e75df00cc
$ crictl logs 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
-0.169075164
-0.169059907
```
