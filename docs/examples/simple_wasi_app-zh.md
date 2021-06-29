# WASI 独立 app

在这个例子中，我们演示了如何从 rust 应用程序构建一个独立的 WASM 应用程序。

## 先决条件

> 如果你只是想要一个 wasm 字节码文件作为容器镜像进行测试，你可以跳过构建过程，只需 [在此处下载 wasm 文件](https://github.com/second-state/wasm-learning/blob/master/ssvm/wasi/wasi_example_main.wasm).

如果不想跳过，请按照这些简单的说明进行操作 [安装 Rust 和 rustwasmc](https://www.secondstate.io/articles/rustwasmc/) 工具链。

## 下载示例代码

```bash
git clone git@github.com:second-state/wasm-learning.git
cd ssvm/wasi
```

## 构建 WASM 字节码

```bash
rustwasmc build
```

wasm 字节码应用程序是在 `target/wasm32-wasi/release/wasi_example_main.wasm` 文件中。你现在可以发布并把它作为容器镜像使用。

## 创建 Dockerfile

在 `pkg` 文件夹中创建名为 `Dockerfile` 的文件，包含以下内容：

```
FROM scratch
ADD wasi_example_main.wasm .
CMD ["wasi_example_main.wasm"]
```

## 创建容器镜像

该例子使用 [buildah](https://github.com/containers/buildah) 来构建镜像。你可以使用任何其它工具来创建容器镜像。

```bash
sudo buildah bud -f Dockerfile -t wasm-wasi-example
sudo buildah push wasm-wasi-example docker://registry.example.com/repository:tag
```

下面是将 wasm 字节码文件发布到公共 Docker hub的示例。

```bash
sudo buildah push wasm-wasi-example docker://docker.io/hydai/wasm-wasi-example:latest
```


## 创建容器配置

创建名为 `container_wasi.json` 的文件，包含以下内容：

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

## 创建沙盒配置文件
创建名为 `sandbox_config.json`的文件，包含以下内容：

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

## 创建 cri-o POD
```bash
# 创建 POD。输出将会和例子不同。
sudo crictl runp sandbox_config.json
7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
# 设置一个辅助变量供以后使用。
POD_ID=7992e75df00cc1cf4bff8bff660718139e3ad973c7180baceb9c84d074b516a4
```

## 创建容器
```bash
# 创建容器实例。输出将会和示例不同。
sudo crictl create $POD_ID container_nbody.json sandbox_config.json
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
```

## 启动容器
```bash
# 列出容器，状态应为“创建”
sudo crictl ps -a

CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   About a minute ago   Created             podsandbox1-wasm-wasi   0                   7992e75df00cc

# 启动容器
sudo crictl start 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f
1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f

# 再次查看容器状态
# 如果容器没有完成工作，你会看到运行状态。
# 因为这个例子很小。此时您可能会看到Exited。
sudo crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   About a minute ago   Running             podsandbox1-wasm-wasi   0                   7992e75df00cc

# 当容器完成。你能看到状态变为 Exited。
sudo crictl ps -a
CONTAINER           IMAGE                           CREATED              STATE               NAME                     ATTEMPT             POD ID
1d056e4a8a168       hydai/wasm-wasi-example:latest   About a minute ago   Exited              podsandbox1-wasm-wasi   0                   7992e75df00cc

# 查看容器记录
sudo crictl logs 1d056e4a8a168f0c76af122d42c98510670255b16242e81f8e8bce8bd3a4476f

Test 1: 打印随机数
Random number: 960251471

Test 2: 打印随机字节
Random bytes: [50, 222, 62, 128, 120, 26, 64, 42, 210, 137, 176, 90, 60, 24, 183, 56, 150, 35, 209, 211, 141, 146, 2, 61, 215, 167, 194, 1, 15, 44, 156, 27, 179, 23, 241, 138, 71, 32, 173, 159, 180, 21, 198, 197, 247, 80, 35, 75, 245, 31, 6, 246, 23, 54, 9, 192, 3, 103, 72, 186, 39, 182, 248, 80, 146, 70, 244, 28, 166, 197, 17, 42, 109, 245, 83, 35, 106, 130, 233, 143, 90, 78, 155, 29, 230, 34, 58, 49, 234, 230, 145, 119, 83, 44, 111, 57, 164, 82, 120, 183, 194, 201, 133, 106, 3, 73, 164, 155, 224, 218, 73, 31, 54, 28, 124, 2, 38, 253, 114, 222, 217, 202, 59, 138, 155, 71, 178, 113]

Test 3: 调用 echo 函数
Printed from wasi: This is from a main function
This is from a main function

Test 4: 打印环境变量
The env vars are as follows.
PATH: /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
TERM: xterm
HOSTNAME: crictl_host
PATH: /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
The args are as follows.
/var/lib/containers/storage/overlay/006e7cf16e82dc7052994232c436991f429109edea14a8437e74f601b5ee1e83/merged/wasi_example_main.wasm
50000000

Test 5: 创建文件 `/tmp.txt` 包含内容 `This is in a file`

Test 6: 从之前文件读取内容
File content is This is in a file

Test 7: 删除之前文件
```
