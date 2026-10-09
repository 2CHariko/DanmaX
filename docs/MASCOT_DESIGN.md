# DanmaX 官方看板娘设定集 (Mascot Design Specification)

本文档定义 **DanmaX** 项目的官方二次元拟人化形象（看板娘）视觉规范、人设档案、软件功能映射与二游级立绘技术参数，供项目 UI 设计、宣传物料、社群衍生及 AI 绘图标准化使用。

---

## 目录
1. [角色档案与基础设定](#1-角色档案与基础设定)
2. [视觉三视图设计规范](#2-视觉三视图设计规范)
3. [核心功能与角色元素映射](#3-核心功能与角色元素映射)
4. [二游级（Gacha Splash）立绘设计规范](#4-二游级gacha-splash立绘设计规范)
5. [UI 交互与状态差分系统](#5-ui-交互与状态差分系统)
6. [AI 绘图工程化提示词库](#6-ai-绘图工程化提示词库)
7. [客户端与项目应用落地规划](#7-客户端与项目应用落地规划)

---

## 1. 角色档案与基础设定

| 属性项 | 详细设定 |
| :--- | :--- |
| **官方代号** | **DanmaX** |
| **角色名** | **艾克斯**（X / Xiao-X / 艾希） |
| **爱称 / 昵称** | 幕小希、X酱、小叉子 |
| **身份定位** | 桌面视界伴随者 · 媒体节拍巡查官 |
| **外貌年龄** | 16 ~ 17 岁（高中少女体态） |
| **身高 / 体态** | 158 cm / 娇小轻盈，体态柔软自然 |
| **生日** | 10 月 24 日（程序员节） |
| **代表色** | 奶霜白（`#FDFDFD`）、马卡龙樱粉（`#FFB6C1`）、清透冰蓝（`#56CCF2`）、星夜深灰（`#2D3238`） |
| **性格特质** | **甜美软萌 + 微傲娇吐槽役**。表面是爱追番、爱吃甜食的软妹子，内心住着一个“吐槽发射器”。一旦看到烂俗桥段或高能剧情，眼神立刻亮起，手速拉满开始发弹幕。 |
| **标志性口癖** | “前方高能，注意看我哦！”、“等等……这段必须暂停吐槽一下！”、“我是透明的啦，点不到我的~” |

---

## 2. 视觉三视图设计规范

```text
       [ 奶灰银蓬松齐肩短发 + 内层粉色挑染 ]
                │
     ┌──────────┴──────────┐
     ▼                     ▼
[马卡龙粉 X型糖果发夹]  [复古金属奶白头戴耳机(挂脖)]
     │                     │
     └──────────┬──────────┘
                ▼
  [奶白落肩慵懒粗针织开衫 (一侧微露肩)]
                │
     ┌──────────┴──────────┐
     ▼                     ▼
[暖杏粉花边修身吊带]    [深灰高腰百褶短裙]
                │
     ┌──────────┴──────────┐
     ▼                     ▼
[右大腿皮质环 + 亚克力吊牌] [纯白微堆堆棉袜 + 浅青配色厚底鞋]
```

### 2.1 头部与五官（Head & Facial Features）
* **发型发色**：
  * **主色**：奶灰银（Milk Ash），带有空气感与半透光泽。
  * **结构**：蓬松齐肩波波头（Fluffy Bob），发尾自然内扣微卷，伴有细碎轻盈的空气刘海。
  * **内层挑染（Inner Color）**：拨开耳后头发隐约可见一层淡樱花粉挑染，增添层次与少女感。
* **面部与神态**：
  * 晶莹透亮的湖水蓝大眼睛，瞳孔深处有细碎的高光星斑。
  * 面颊自然带有淡淡的蜜桃粉红晕，神情甜美温和，嘴角常带着浅浅的微笑或微鼓的小嘴。
* **核心发饰（Logo 载体）**：
  * 右侧刘海别着**两枚马卡龙粉色的细长糖果质感水滴一字发夹，自然交叉成“X”字形**。
  * 告别冰冷的机械发卡，以少女日常小饰品自然呈现 **DanmaX** 的“X”精神。

### 2.2 服饰与层次（Outfit Layers）
* **外层（针织开衫）**：
  * 宽松软糯的奶白色落肩针织毛衣开衫（Oversized Cardigan），粗线纹理，质地蓬松。
  * 一侧肩头自然微滑，露出一侧锁骨与肩带，展现慵懒舒适感。
* **内搭（吊带背心）**：
  * 蜜桃浅粉/暖杏色微短款修身吊带衫，边缘有细腻微卷花边。
* **下装（学院短裙）**：
  * 深灰色高腰百褶短裙（Pleated Mini Skirt），裙褶利落，中和上身的软糯膨胀感。
* **腿部与足部（Legwear & Shoes）**：
  * **右大腿环**：一条纤细的浅灰皮革腿环，中间卡扣挂着一枚微型半透明亚克力 **DanmaX** Logo 吊牌。
  * **袜套**：纯白色微堆堆棉袜（Slouchy Socks），自然堆叠在脚踝处。
  * **鞋履**：复古米白厚底老爹鞋，带有浅青/冰蓝色细节边线。

### 2.3 核心道具（Props）
* **头戴式耳机（SMTC 媒体感知意象）**：
  * 奶白色配浅香槟金金属边框的复古超薄头戴式耳机。
  * 常年随意挂在脖颈上，耳罩海绵轻轻贴着锁骨，不压发型，生活感拉满。

---

## 3. 核心功能与角色元素映射

| 软件功能特性 | 拟人化视觉/人设映射 | 设计考量 |
| :--- | :--- | :--- |
| **桌面透明覆盖层 (Overlay)** | **通透感视觉表现**：微透光的针织质感、半透水滴发夹、空气感发丝、轻盈透明的 UI 气泡。 | 避免厚重视觉遮挡，强化“不遮挡主内容”的软件特性。 |
| **Windows SMTC 媒体联动** | **奶白色头戴耳机**：常挂颈间，身体随系统媒体节拍轻晃；暂停时角色会跟着“断电发呆”。 | 将系统 API 监听转化为生活化的“听歌感知”动作。 |
| **弹幕调度与轨道 (Danmaku Tracks)** | **周身悬浮弹幕**：身侧斜向飘过的半透明全息聊天气泡、彩色高能文字条与小表情贴纸。 | 动态表现弹幕的飞行流动感。 |
| **C++20 & 极致轻量性能** | **少女体态的轻盈感**：无厚重机甲，动作灵动，无感驻留。 | 契合内存占用极低、零冗余依赖的代码架构。 |
| **鼠标穿透模式 (Click-Through)** | **“幽灵状态”能力**：被鼠标滑过时会变成 30% 半透明虚影，轻轻吐舌头。 | 拟人化呈现非置顶焦点不夺取鼠标事件的物理特性。 |

---

## 4. 二游级（Gacha Splash）立绘设计规范

```text
               [ 飘扬的发丝与软萌发夹 (X) ]
                         │
      ┌──────────────────┴──────────────────┐
      ▼                                     ▼
[半透明悬浮弹幕气泡]                      [立体音频声波粒子]
      │                                     │
      └──────────────────┬──────────────────┘
                         ▼
        [ 核心动态立绘：单手按耳机 + 身体前倾微动势 ]
                         │
      ┌──────────────────┴──────────────────┐
      ▼                                     ▼
[飘动的奶白针织衫衣角]                  [微风吹拂的百褶裙摆]
                         │
                         ▼
               [ 纯白堆堆袜 + 浅青鞋履 ]
```

### 4.1 构图与姿态（Dynamic Composition）
* **整体动势**：重心落在单腿的微仰倾斜站姿，另一只脚自然前探，身体带有一点 S 型曲线与向前倾斜的互动感。
* **肢体动作**：
  * 右手轻搭在颈间的奶白耳机边缘，仿佛正在聆听或摘下耳机与观众交谈；
  * 左手微抬，掌心轻捧或在空中划过一条浅青色光轨；
  * 衣摆、发丝、短裙裙角在微风与动势下自然轻扬飘动。

### 4.2 背景与立绘特效（Splash FX）
* **半透明全息弹幕流**：数道半透明、带微弱外发光的浅蓝/浅粉矩形弹幕文本框自左下向右上斜向飞过，里面带有“23333”、“awsl”、“前方高能”等模糊文字。
* **声波与粒子**：脚底与身侧环绕着如同微风般的音频均衡器波纹（Equalizer waves）以及细小光斑。
* **光影氛围**：柔和清透的摄影棚侧逆光（Rim Light），勾勒出发丝与毛衣边缘的金色绒毛光感，背景为纯净的微渐变清透光晕。

---

## 5. UI 交互与状态差分系统

| 软件状态 | 角色表情与肢体反应 | 应用场景 |
| :--- | :--- | :--- |
| **就绪 / 监听中** | 单手托腮，大眼睛眨巴眨巴，挂着耳机抿嘴微笑 | 软件主面板日常常驻、设置页头部 |
| **媒体播放中** | 戴上耳机，单脚轻轻打节拍，头顶冒出小音符光标 | 识别到 SMTC 会话正在播放时 |
| **媒体暂停中** | 摘下半边耳机，两眼微微放空，趴在虚拟窗口边框上发呆 | 视频暂停时的小浮窗提示 |
| **弹幕高能井喷** | 双手捧脸作惊讶状，眼睛闪亮亮，脸颊通红 | 高密度弹幕出现时的彩蛋效果 |
| **穿透模式开启** | 身体半透明化（Alpha 40%），单手比“嘘”的可爱手势 | 切换为穿透模式时的短暂反馈 |
| **未连接 / 空闲** | 怀里抱着软绵绵的彩虹小兔抱枕，懒洋洋地蜷坐着 | 尚未加载弹幕或媒体列表为空时 |

---

## 6. AI 绘图工程化提示词库

### 6.1 全自然语言超细致正向提示词（Natural Language Character Prompt）

```text
A full-body anime character design of a cute, sweet teenage girl standing alone against a pure solid white background. She has fluffy, voluminous milk-ash silver-grey bob hair with soft wavy ends and delicate airy bangs, with soft pastel sakura-pink inner-hair highlights gently showing near her nape and behind her ears. On the right side of her bangs, two slender pastel candy-pink bobby pins are neatly crossed into a distinct "X" shape. She has crystal-clear aqua-blue eyes with sparkling starlight highlights, long delicate eyelashes, softly blushing peach cheeks, and a warm, cheerful, gentle smile.

Around her slender neck rests a stylish retro over-ear headphone with a creamy-white matte shell, champagne-gold metallic hinge accents, and soft mint-cyan ear cushions, draped casually without pressing into her hair.

She wears an oversized, chunky cable-knit cardigan in soft cream-white with distinct wool ribbing, cozy drop-shoulder sleeves, and round beige buttons down the front; the cardigan slips slightly off her right shoulder to reveal her delicate collarbone and shoulder line. Underneath, she wears a fitted cropped camisole in soft pastel peach-pink with a delicate micro-lace trim along the neckline. On her left wrist, she wears a dainty pastel beaded bracelet with a tiny cyan bead charm.

On her lower body, she wears a high-waisted dark charcoal-grey pleated tennis skirt with sharp, crisp pleats falling to mid-thigh, secured at the waistband with a slim dark belt and a polished silver buckle. On her right upper thigh, she wears a stylish thin white leather double-strap garter harness with small silver buckles, adorned with a tiny hanging acrylic DanmaX badge.

Her legs are bare and smooth, leading down to loose, slouchy pure-white ribbed cotton scrunch socks casually bunched around her ankles and shins. On her feet, she wears retro chunky platform sneakers in clean white leather accented with vibrant mint-cyan paneling and pastel-pink shoelaces.

She is posed in a graceful and dynamic standing posture with her body angled slightly forward and her weight shifted onto one leg. Her right hand gently touches the retro headphone resting on her neck, while her left hand is extended outward in a welcoming, open gesture with slender, natural fingers. Her cardigan hem and hair strands billow gently in a subtle breeze.

The background is completely seamless, pure solid white with zero floating ribbons, zero streamers, zero background scenery, zero UI elements, zero virtual screens, and zero particles. The illustration features clean, crisp anime line art, soft professional studio rim lighting, delicate fabric textures, vibrant and harmonious pastel coloring, masterpiece quality, 8k resolution.
```

### 6.2 负向提示词（Negative Prompt · 强力去除飘带与多余特效）

```text
(worst quality, low quality:1.4), (deformed, distorted, disfigured:1.3), poorly drawn,
bad anatomy, wrong anatomy, extra limb, missing limb, floating limbs, (mutated hands and fingers:1.4),
disconnected limbs, mutation, ugly, disgusting, blurry, amputation,
(floating ribbons, ribbons, streamers, floating strings, swirling cloth, magic swirls, vortex:1.5),
(background, complex background, scenery, outdoors, indoors, room, wall, floor, furniture, shadow on background:1.4),
(HUD, UI, virtual screen, holographic screen, hologram, floating screen, monitor, interface:1.5),
(glowing effects, laser, neon lines, sparkles, particle effects, magic circles, lens flare:1.4),
text, watermark, signature, borders, frames, bubbles, dialog boxes
```
### 6.3 推荐生成参数建议

| 参数 | 推荐值 | 说明 |
| :--- | :--- | :--- |
| **Model** | AnyV5 / Counterfeit / NovelAI / Animagine XL 3.1 | 适合日系清透萌系二游风格模型 |
| **Sampler** | DPM++ 2M Karras 或 Euler a | 保留细节平滑度与绒毛光感 |
| **Steps** | 28 ~ 36 | 充分解析发丝内层与毛衣针织纹理 |
| **CFG Scale** | 7.0 ~ 8.0 | 维持画面色彩鲜亮而不过度饱和 |
| **Resolution** | 832×1216 或 1024×1536 | 竖屏立绘比例，后续可加 1.5x Hires.fix |

---

## 7. 客户端与项目应用落地规划

1. **客户端“关于”面板 (About Page)**：
   * 集成艾克斯的半身像素/Q版立绘或二游常驻立绘插画。
   * 点击发夹可触发“前方高能”彩蛋声效或弹幕小彩蛋。
2. **空状态占位图 (Empty State Graphics)**：
   * 在“未选择媒体会话”、“弹幕列表为空”时，展示艾克斯抱着彩虹兔抱枕的慵懒 Q 版插画。
3. **开源仓库门面 (GitHub Readme & Badges)**：
   * 在根目录 README 头部增加官方看板娘欢迎横幅（Banner）与状态表情包贴纸。
4. **透明覆盖层水印/挂件（可选开关）**：
   * 提供“桌面萌物伴随”微型挂件模式，作为可选功能让小艾克斯常驻在屏幕角落打节拍。
