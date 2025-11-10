后端线程 (30 FPS):
SimulationController::start()
    └─ SimulationEngine::simulation_loop()
        └─ 每 33ms 执行一次：
            ├─ update_species()
            ├─ handle_reproduction()
            ├─ cleanup_dead()
            └─ update_statistics()

前端线程 (1 FPS):
Widget::updateFrame() (每 1000ms)
    └─ controller->get_data()  ← 获取数据快照
        └─ 返回 EcosystemStateData
            └─ paintEvent() 绘制

两个线程通过 get_data() 通信（只读操作，线程安全）