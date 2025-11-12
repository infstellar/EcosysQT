目录说明
===========

此目录用于存放地形材质贴图（由用户提供的图片）。我已创建目录占位，等待你把贴图放入。

命名和切片约定（建议）
-----------------------
为了便于代码按位置稳定选择变体并能用类似 `img = big_img[pos_x % 5][pos_y % 5]` 的方式渲染，建议采用以下约定：

- 每类地形（biome）放在同一目录下，文件名以 `<biome>_<index>.png` 命名。
  - 示例：`plains_0.png`, `plains_1.png`, ..., `desert_0.png`, `hills_0.png`, `river_0.png`
- 建议每类至少放 3~5 个变体（index 从 0 开始）。
- 如果你更喜欢把所有变体放在一张大图里（sprite sheet / tile atlas），也可以：
  - 名称例如 `plains_atlas.png`，并在提交时告诉我 atlas 的列数/行数（例如 5×5），我会按 `pos_x % cols` 和 `pos_y % rows` 切片。

推荐 biome 列表（可扩展）
- plains
- desert
- hills
- river
- forest (可选)

切片/选择算法说明
------------------
当我开始实现渲染代码时，会支持两种模式：

1) 单文件多变体（`plains_0.png` 等）
   - 加载所有变体到一个 vector，然后选择 index = (pos_x * 73856093u + pos_y * 19349663u) % variants.size() 或更简单的 `((pos_x % w) * (pos_y % h)) % variants.size()`，保证稳定且分布均匀。

2) 大图 atlas（`plains_atlas.png`）
   - 需要你告诉我 atlas 的列数和行数（例如 5×5），我会按 `col = pos_x % cols`，`row = pos_y % rows` 切片为单个 tile 纹理。

下一步（我会做的）
-----------------
- 你把贴图放到 `resources/images/terrain/` 后回复我我就开始：
  1) 把这些图片加入 `resources/app.qrc`（或更新 qrc），
  2) 在前端 `SimulationRenderer` 中实现基于 tile 位置的变体选择并渲染地形层（先用你希望的 atlas/变体方式），
  3) 在后端 MapGenerator 中标记 `Tile.has_river`，前端会用 river 贴图覆盖河道。

路径（项目内）
-------------
- 贴图目录（当前已创建）：
  `resources/images/terrain/`

如果你愿意，现在就把图片拖到上面的目录并告诉我：
- 每类图片是单独小图（`plains_0.png`...）还是 atlas（`plains_atlas.png`）？
- 如果是 atlas，请告诉我每个 atlas 的列数和行数（例如 5×5）。

我收到图后就开始把它们加入资源并实现渲染。