# 简介

**RUNW** 是一个 OCI 兼容的运行时，用于在容器环境中运行 WASI 启用的 WebAssembly 文件。我们会示范如何使用 CRI-0 工具。

# 开始

## 先决条件

请安装以下容器管理工具。

* [cri-o](https://cri-o.io/)
* [crictl](https://github.com/kubernetes-sigs/cri-tools)
* [containernetworking-plugins](https://github.com/containernetworking/plugins)
* Optional [buildah](https://github.com/containers/buildah) or [docker](https://github.com/docker/cli) for building container image

## 使用 pre-built runw

### 环境

我们预先构建的二进制文件是基于“ubuntu 20.04”的，它有以下依赖项:

* libLLVM-10

你需要使用以下方法安装依赖项:

```bash
sudo apt install -y \
        llvm-10-dev \
        liblld-10-dev
```

### 从发布页面获得预先构建的 runw

```bash
wget https://github.com/second-state/runw/releases/download/0.1.0/runw
```

> 如果你没有使用 Ubuntu 20.04，你需要构建自己的 RUNW 二进制文件。按照附录中的说明操作。

## 安装 runw 到 cri-o

```bash
# 获得 wasm-pause 实用程序
sudo crictl pull docker.io/beststeve/wasm-pause

# 安装 runw 到 cri-o
sudo cp -v runw /usr/lib/cri-o-runc/sbin/runw
sudo chmod +x /usr/lib/cri-o-runc/sbin/runw
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

## 重启 cri-o

```bash
sudo systemctl restart crio
```

# 例子

## 简单的 Wasi 应用

在这个示例中，我们将演示如何创建一个简单的 rust 应用程序，以获取程序参数、检索环境变量、生成随机数、将字符串打印到 stdout 并创建文件。


要创建docker镜像和应用程序细节，请参考[简单 Wasi 应用](docs/examples/simple_wasi_app.md).

### 下载 wasi-main docker 镜像

We've create a docker image called `wasi-main` which is a very light docker image with the `wasi_example_main.wasm` file.我们用`wasi_example_main.wasm` 文件创建了一个名为“ wasi-main”的 docker 镜像， 是一个非常轻的 docker 镜像。

```bash
sudo crictl pull docker.io/hydai/wasm-wasi-example
```

### 创建容器配置

创建名为 `container_wasi.json` 的文件，有如下内容：

```json
{
  "metadata": {
    "name": "podsandbox1-wasm-wasi"
  },
  "image": {
    "image": "hydai/wasm-wasi-example:latest"
  },
  "args": [
    "wasi_example_main.wasm", "50000000"
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
```

### 创建沙盒配置文件
创建名为 `sandbox_config.json` 的文件，包含如下内容：

```json
{
  "metadata": {
    "name": "podsandbox12",
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
```

### 创建 cri-o POD
```bash
# 创建 POD。输出将和例子显著不同。
sudo crictl runp sandbox_config.json
7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
# Set a helper variable for later use.
POD_ID=7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
```

### 创建容器
```bash
# 创建容器机器。输出将和例子显著不同。
sudo crictl create $POD_ID container_wasi.json sandbox_config.json
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
```

### 启动容器
```bash
# 列出容器，状态应该为`Created` 已创建
sudo crictl ps -a

CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   About a minute ago   Created             podsandbox1-wasm-wasi   0                   7992e75df00cc

# 启动容器
sudo crictl start 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f

# 再次检查容器状态
# 如果容器未完成其工作，会看到状态为正在运行 Running 。
# 应为该例子非常小。你可能在这时候看到已退出 Exited 。
sudo crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   约1分钟前           Running             podsandbox1-wasm-wasi   0                   7992e75df00cc

# When the container is finished. You can see the state becomes Exited.
sudo crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   约1分钟前          Exited              podsandbox1-wasm-wasi   0                   7992e75df00cc

# 查看容器的记录
sudo crictl logs 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f

Test 1: 打印随机数
随机数: 960251471

Test 2: 打印随机字节
Random bytes: [50, 222, 62, 128, 120, 26, 64, 42, 210, 137, 176, 90, 60, 24, 183, 56, 150, 35, 209, 211, 141, 146, 2, 61, 215, 167, 194, 1, 15, 44, 156, 27, 179, 23, 241, 138, 71, 32, 173, 159, 180, 21, 198, 197, 247, 80, 35, 75, 245, 31, 6, 246, 23, 54, 9, 192, 3, 103, 72, 186, 39, 182, 248, 80, 146, 70, 244, 28, 166, 197, 17, 42, 109, 245, 83, 35, 106, 130, 233, 143, 90, 78, 155, 29, 230, 34, 58, 49, 234, 230, 145, 119, 83, 44, 111, 57, 164, 82, 120, 183, 194, 201, 133, 106, 3, 73, 164, 155, 224, 218, 73, 31, 54, 28, 124, 2, 38, 253, 114, 222, 217, 202, 59, 138, 155, 71, 178, 113]

Test 3: 调用 echo 函数
打印自 wasi: 这是来自主函数
这是来自主函数

测试 4: 打印环境变量
The env vars are as follows.
PATH: /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
TERM: xterm
HOSTNAME: crictl_host
PATH: /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
The args are as follows.
/var/lib/containers/storage/overlay/006e7cf16e82dc7052994232c436991f429109edea14a8437e74f601b5ee1e83/merged/wasi_example_main.wasm
50000000

测试 5: 创建文件 `/tmp.txt` 内容为 `This is in a file`

Test 6: 从之前的文件读取内容
文件内容为 “This is in a file(这在一个文件中)”

Test 7: 删除之前的文件
```

# 附件

## 从源开始创建

### 获得源代码

```bash
$ git clone git@github.com:second-state/runw.git
$ cd runw
$ git checkout 0.1.0
```

### 准备环境

#### 使用 docker 镜像

我们的 docker 镜像使用 `ubuntu 20.04` 作为基础。

```bash
$ docker pull secondstate/runw
```

#### 或者手动设置环境

```bash
# 工具和库
$ sudo apt install -y \
        software-properties-common \
        cmake \
        libboost-all-dev \
        libsystemd-dev

# 你将会需要安装 llvm
$ sudo apt install -y \
        llvm-10-dev \
        liblld-10-dev

# RUNW 支持 clang++ 和 g++ 编译器
# 可以选择其中之一来创建此项目
$ sudo apt install -y gcc g++
$ sudo apt install -y clang
```
创建 RUNW

```bash
# 在拉去我们的 runw docker 镜像
$ docker run -it --rm \
    -v <path/to/your/runw/source/folder>:/root/runw \
    secondstate/runw:latest
(docker)$ cd /root/runw
(docker)$ mkdir -p build && cd build
(docker)$ cmake -DCMAKE_BUILD_TYPE=Release .. && make -j
(docker)$ exit
```
