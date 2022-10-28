# Directories #

**DATControlCenter**: 分布式测试控制中心。

**DATDeployer**: 测试执行机控制端。负责部署、启动 测试执行程序，汇报测试执行机状态，并执行其系统指令。

**DATController**: 用户测试控制端目录。

**DATController/DATStatus**: 查询当前分布式测试控制中心可用的测试执行程序，已经部署的测试执行程序，可用的测试执行机，正在运行的测试执行程序。

**DATController/DATMachineStatus**: 监控所有测试执行机的状态（刷新周期：2s）。

**DATController/DATActorUploader**: 向分布式测试控制中心上传测试执行程序。

**DATController/DATDeployController**: 向测试执行机部署分布式测试控制中心的测试执行程序。

**DATController/DATAction/DATAction**: 通过分布式测试控制中心向正在执行的测试执行程序发送控制命令。

**DATController/DATAction/DATActionAll**: 通过分布式测试控制中心向正在执行的所有测试执行程序发送控制命令。

**DATController/Prototype**: 通用的用户测试控制端 demo。

**DATActor**: 测试执行程序目录。

**DATActor/Prototype**: 通用的测试执行程序 demo。