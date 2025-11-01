#include <QApplication>
#include "mainwindow.h"
#include "backend/include/species_factory.h"
#include "backend/include/species_config_provider.h"
#include <QCoreApplication>
#include <QDir>
#include <iostream>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    
    // 注入 YAML 配置提供者并注册所有物种到工厂
    {
        auto provider = std::make_shared<YamlSpeciesConfigProvider>(QDir::currentPath().toStdString());
        // 简单运行时校验：读取并打印 tiger 关键参数
        {
            TigerParams tp = provider->get_tiger_params();
            std::cout << "[Config] Tiger params: energy=" << tp.energy
                      << ", max_age=" << tp.max_age
                      << ", move_speed=" << tp.movement_speed << std::endl;
        }
        g_species_factory.set_config_provider(provider);
        register_all_species();
    }
    
    MainWindow w;
    w.show();
    return a.exec();
}