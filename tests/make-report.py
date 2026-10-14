from pathlib import Path
import json,hashlib
R=Path(__file__).resolve().parents[1]; E=R/'scratch/evidence'
p=json.loads((E/'performance.json').read_text()); f=json.loads((E/'functional.json').read_text())
MiB=1024**2
rows=p['samples']; main=[x for x in rows if 'p95' in x];fx=next(x for x in rows if '3_cycles' in x['scenario'])
assert not fx['rest_active_after'] and not fx['covers_after']
names={'visible_glass':'磨砂浮窗','paused_glass':'暂停','hidden_running':'隐藏且计时','visible_solid':'实色浮窗','visible_glass_soak':'磨砂浮窗持续运行'}
lines=['---','title: 轻计时 · Windows 原生版实测','subtitle: 本机数据 · 2026-10-10 · 便携单文件','template: doc','theme: shadcn','mode: auto','style: 80','---','','## A 可运行原生版','','C++ / Win32 原生版已编译。双击 LightTimer.exe 即可运行。','文件大小：878 KiB。无需安装额外运行库。','常驻浮窗为 116 × 36 DIP。一小时及以上宽度为 152 DIP。','','## B 内存与 CPU','','内存列为每秒采样的 P95。CPU 为进程累计 CPU 时间除以测量时长。','CPU 百分比按单核计算，没有除以 12 个逻辑处理器。','','| 场景 | 时长 | 私有工作集 | 总工作集 | 私有提交 | CPU 时间 | 单核占比 |','| --- | --- | --- | --- | --- | --- | --- |']
for x in main:
 m=x['p95'];lines.append(f"| {names[x['scenario']]} | {x['seconds']:.0f} s | {m['private_working_set']/MiB:.2f} MiB | {m['working_set']/MiB:.2f} MiB | {m['private_commit']/MiB:.2f} MiB | {x['cpu_seconds']:.4f} s | {x['single_core_percent']:.4f}% |")
lines+=['','私有工作集表示只属于此进程的驻留页面。总工作集也含共享系统 DLL 页面。','私有提交表示系统为进程承诺的私有内存。这三个指标不能互相替代。','30/60 秒短样本受计量粒度和桌面活动影响，不能据此判断磨砂与实色的耗电高低。','','## C 重绘与资源稳定性','','| 场景 | 绘制次数 | 计时回调 | GDI 对象 | USER 对象 |','| --- | --- | --- | --- | --- |']
for x in main:lines.append(f"| {names[x['scenario']]} | {x['paints']} | {x['callbacks']} | {x['gdi_before']} → {x['gdi_after']} | {x['user_before']} → {x['user_after']} |")
lines+=['','回调列只记录应用计时调度，不是系统级唤醒次数。',f"三轮呼吸与 5 秒休息遮罩，共 {fx['seconds']:.1f} 秒，累计 CPU {fx['cpu_seconds']:.4f} 秒。",f"动效期间私有工作集峰值 {fx['peak']['private_working_set']/MiB:.2f} MiB；结束后 {fx['after']['private_working_set']/MiB:.2f} MiB。",f"动效期间私有提交峰值 {fx['peak']['private_commit']/MiB:.2f} MiB；结束后 {fx['after']['private_commit']/MiB:.2f} MiB。",'最后一轮遮罩已关闭。','','','## D 交互验证','','较早完整回归的 12 项原生窗口测试通过。覆盖计时、悬停、暂停、设置和休息遮罩。','','最后一次追加回归在暂停重绘断言处未通过，原因尚未确认。独立的 30 秒暂停采样中，重绘和计时回调均为 0。','按用户要求，已停止追加测试。','较早 MSAA 测试通过：名称、角色、输入值和操作。','测试通过原生窗口消息和系统辅助功能接口驱动真实 EXE。','实际鼠标与键盘输入测试通过：倒计时运行中，重新输入 00:07:13 并开始。','当前构建的 Esc 恢复刷新专项检查通过。','输入框提供黑白选中态、文本指针和静态插入光标。','检查了实际窗口截图。未把 HTML 原型截图当作原生结果。','','## E 原生窗口截图','','运行中：','','![运行中的原生浮窗](running.png)','','悬停后：','','![原位替换后的控制按钮](hover.png)','','结束后：','','![无文字结束图标](complete.png)','','设置：','','![原生设置](settings-rest.png)','','## F 测量方法与边界','','Windows 10 家庭中文版 22H2，Build 19045。Intel i7-10750H，6 核 12 线程。','单屏 1920 × 1080，实际缩放 125%。','持续采样使用 25 分钟，浮窗为 116 × 36 DIP。','先完成主要交互检查，再预热 60 秒。每秒读取一次进程资源。没有压缩或清空工作集。','长期采样时关闭提示音；动效测试使用三轮呼吸和休息遮罩。','这次采样处于普通桌面会话。','','暂停、隐藏场景测得的零 CPU 是采样分辨率内为零，不代表绝对零能耗。','没有测量整机瓦数、电池续航或 DWM 的 GPU 成本。没有同机对比 Windows 时钟。','本次完成一轮 10 分钟持续运行；尚未完成三轮重复、1 小时计时或真实睡眠唤醒验收。','Windows 11、多显示器、实际混合 DPI、讲述人完整流程仍需专项真机验证。','休息遮罩覆盖普通桌面，不承诺覆盖安全桌面和独占全屏。','','## G 复现与交付','','[程序](../../dist/LightTimer.exe)','[操作与构建说明](../../README.md)','最终资源数据：performance.json。','当前专项检查：keyboard-after.json、escape.json。','较早回归记录：functional.json、accessibility.json。','较早边界与持久化记录：boundaries.json、persistence.json。','functional.json 保留较早成功结果。最后一次失败未覆盖该文件。','','较早版本的中断采样文件已保留，但未纳入本报告。','构建 SHA-256：','','```text',p['binary_sha256'],'```','']
b=json.loads((E/'boundaries.json').read_text());a=b['audio_memory']
lines[lines.index('## E 原生窗口截图'):lines.index('## E 原生窗口截图')]=[
 f"较早边界回归的 {len(b['checks'])} 项检查通过。详情保留在 boundaries.json。",
 '缩放测试发送 WM_DPICHANGED 消息，不能替代实际混合 DPI 多屏测试。',
 f"提示音结束约 2 秒后的快照：私有工作集 {a['after']['private_working_set']/MiB:.2f} MiB，总工作集 {a['after']['working_set']/MiB:.2f} MiB。",
 '提示音路径已运行并回收缓冲；未做声学录制验收。','较早持久化测试通过：载入偏好、复用单实例、保存时长和重启状态。','']
assert p['binary_sha256']==hashlib.sha256((R/'dist/LightTimer.exe').read_bytes()).hexdigest()
(E/'report.md').write_text('\n'.join(x + ('\n' if i > 8 and x and not x.startswith(('|', '```')) and x != p['binary_sha256'] else '') for i,x in enumerate(lines)),encoding='utf-8')
print(E/'report.md')
