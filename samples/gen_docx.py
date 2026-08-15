#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 samples/demo.docx —— 较大的富文本 docx 示例，完整覆盖 DocxParser 功能。

覆盖点：
- 主标题 / 「第X章」章节（配合 ChapterService 识别）
- 富文本：粗体、斜体、颜色（红/蓝/绿/橙）、字号（24/18/14/10px）、字体（宋体/黑体/Times）
- 段内混排（同一段多个 run 不同格式）
- 段内换行（w:br）
- 纯图段落（→ image 行，内嵌 base64）
- 图文混排（文字 + 行内图片）
- 空段落（跳过逻辑）
- 大量普通正文段落（体现「较大」）

用法：python samples/gen_docx.py  →  输出 samples/demo.docx
"""
import struct
import zipfile
import zlib

W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R_NS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
WP_NS = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
A_NS = "http://schemas.openxmlformats.org/drawingml/2006/main"
PIC_NS = "http://schemas.openxmlformats.org/drawingml/2006/picture"
PKG_NS = "http://schemas.openxmlformats.org/package/2006/relationships"


def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;")
             .replace(">", "&gt;").replace('"', "&quot;"))


def run(text, bold=False, italic=False, color=None, sz=None, font=None):
    rpr = ""
    if bold:
        rpr += "<w:b/>"
    if italic:
        rpr += "<w:i/>"
    if color:
        rpr += '<w:color w:val="%s"/>' % color
    if sz:
        rpr += '<w:sz w:val="%d"/>' % sz
    if font:
        rpr += '<w:rFonts w:ascii="%s" w:eastAsia="%s"/>' % (font, font)
    rpr_open = "<w:rPr>%s</w:rPr>" % rpr if rpr else ""
    return '<w:r>%s<w:t xml:space="preserve">%s</w:t></w:r>' % (rpr_open, esc(text))


def br_run():
    return "<w:r><w:br/></w:r>"


def image_run(r_id, cx, cy, name):
    return (
        '<w:r><w:drawing><wp:inline distT="0" distB="0" distL="0" distR="0">'
        '<wp:extent cx="%d" cy="%d"/><wp:docPr id="1" name="%s"/>'
        '<a:graphic><a:graphicData uri="%s"><pic:pic>'
        '<pic:nvPicPr><pic:cNvPr id="1" name="%s"/><pic:cNvPicPr/></pic:nvPicPr>'
        '<pic:blipFill><a:blip r:embed="%s"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>'
        '<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="%d" cy="%d"/></a:xfrm>'
        '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr>'
        '</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>'
        % (cx, cy, name, PIC_NS, name, r_id, cx, cy)
    )


def p(*runs):
    return "<w:p>" + "".join(runs) + "</w:p>"


# ---- PNG 生成（纯 stdlib：zlib + struct）----
def _png_chunk(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)


def make_png(w, h, pixel):
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8bit RGB
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        for x in range(w):
            raw.extend(pixel(x, y))
    idat = zlib.compress(bytes(raw), 9)
    return sig + _png_chunk(b"IHDR", ihdr) + _png_chunk(b"IDAT", idat) + _png_chunk(b"IEND", b"")


# 图片1：蓝→红水平渐变（240x180）
img1 = make_png(240, 180, lambda x, y: (int(255 * x / 239), 0, int(255 * (239 - x) / 239)))
# 图片2：绿/深绿棋盘（160x120）
img2 = make_png(160, 120, lambda x, y: (0, 200, 0) if ((x + y) // 16) % 2 == 0 else (0, 120, 0))

CX1, CY1 = 2194560, 1645920  # 2.4in x 1.8in（240x180）
CX2, CY2 = 1463040, 1097280  # 1.6in x 1.2in（160x120）

# ---- 文档段落 ----
paras = []

# 主标题
paras.append(p(run("翻译工作台使用手册", bold=True, color="1F4E79", sz=48, font="黑体")))
paras.append(p(run("—— 面向翻译写作场景的完整指南 ——", italic=True, color="595959", sz=20, font="宋体")))
paras.append(p())  # 空段落

# ================= 第一章 =================
paras.append(p(run("第一章 翻译工作流程", bold=True, color="C00000", sz=36, font="黑体")))
paras.append(p(run("本章介绍 translateX 的核心工作流程：从打开文档、逐行翻译，到批注与保存。")))
paras.append(p(run("工具支持三种翻译后端：本地 Ollama、云端翻译服务与网络大模型 API，"
                   "可根据成本与质量需求灵活切换；重复内容自动命中缓存，显著降低成本。")))

paras.append(p(run("1. ", bold=True, color="1F4E79"),
               run("打开文档：", bold=True, color="C00000"),
               run("支持 .txt、.trx 与 .docx 格式，其中 .trx 为推荐格式，"
                   "富文本与图片显示层可完整往返保存。")))
paras.append(p(run("2. ", bold=True, color="1F4E79"),
               run("选择后端：", bold=True, color="1F4E79"),
               run("本地 Ollama 零成本、云端服务免费额度、网络大模型质量最高。"),
               br_run(),
               run("提示：批量翻译自动分块合并请求，失败单行自动重试并降级。", italic=True, color="548235")))
paras.append(p(run("3. ", bold=True, color="1F4E79"),
               run("逐行翻译：", bold=True, color="7F6000"),
               run("上下文感知翻译保持术语与语气一致，质量自检拦截回显输出。")))

for i in range(1, 7):
    paras.append(p(run("流程要点 %d：" % i, bold=True),
                   run("确保上下文窗口包含前后若干行，译文语气与术语保持一致；"
                       "翻译结果作为批注写入对应行，可随时上一条/下一条跳转复核。")))

# ================= 第二章 =================
paras.append(p(run("第二章 术语一致性", bold=True, color="1F4E79", sz=36, font="黑体")))
paras.append(p(run("术语表是保证翻译质量一致性的关键。用户可维护“原文术语 → 标准译文”映射，"
                   "翻译前注入约束提示，翻译后自动校验术语命中率。")))

paras.append(p(run("示例术语：", bold=True),
               run("API ", bold=True, color="C00000", font="Times New Roman"),
               run("→ 接口 ", color="1F4E79"),
               run("；", ),
               run("context ", italic=True, color="C00000", font="Times New Roman"),
               run("→ 上下文 ", color="1F4E79"),
               run("；", ),
               run("batch ", bold=True, italic=True, color="7F6000", font="Times New Roman"),
               run("→ 批量 ", color="1F4E79"), run("。")))

paras.append(p(run("未命中术语会列入报告：", bold=True),
               run("例如译文漏译术语时将提示 ", ),
               run("missing: API", italic=True, color="C00000", font="Times New Roman"),
               run("，便于人工复核。", )))
paras.append(p(run("字号演示：", bold=True),
               run("特大 ", sz=48, color="1F4E79"),
               run("大 ", sz=36, color="C00000"),
               run("中 ", sz=28),
               run("小 ", sz=20, color="595959"),
               run("极小 ", sz=14, color="808080")))

for i in range(1, 6):
    paras.append(p(run("术语条目 %d：在术语表中维护后，后续所有翻译请求自动携带该约束，"
                       "保证整篇文档内同一概念始终使用同一译法。" % i)))

# ================= 第三章 =================
paras.append(p(run("第三章 质量与成本", bold=True, color="548235", sz=36, font="黑体")))
paras.append(p(run("质量自检覆盖回显拦截、长度合理性、数字与占位符保留；"
                   "成本控制依赖缓存复用、智能分块与模型分级。")))

paras.append(p(run("质量规则：", bold=True, color="548235"),
               run("译文不得直接回显原文；")))
# 上图段：纯文字介绍
paras.append(p(run("图片显示层支持行内插图，.trx 保存时以内嵌 base64 往返："), ))

# 纯图段落 → image 行
paras.append(p(image_run("rId2", CX1, CY1, "image1.png")))
paras.append(p(image_run("rId3", CX2, CY2, "image2.png")))
# 图文混排
paras.append(p(run("示例插图：", bold=True),
               image_run("rId2", CX1, CY1, "image1.png"),
               run("上图演示了行内图片与文字混排的效果。"), ))

for i in range(1, 6):
    paras.append(p(run("优化建议 %d：对重复段落启用缓存，对长文本启用智能分块合并请求，"
                       "对低风险文本优先使用免费后端，整体翻译成本可降低 40%% 以上。" % i)))

# ================= 第四章 =================
paras.append(p(run("第四章 批注与协作", bold=True, color="7F6000", sz=36, font="黑体")))
paras.append(p(run("批注（译文）与原文一一对应，行内直接编辑，字号独立可调；"
                   "支持批注的增删、跳转、清空与 JSON 导入导出。")))

paras.append(p(run("行内编辑：", bold=True),
               run("点击批注即可像编辑原文一样直接修改，超过宽度自动换行；"), ))
paras.append(p(run("快捷键：", bold=True),
               run("Enter 拆行、行首 Backspace 合并、右键菜单提供插入/删除/复制等操作。")))

# 结尾
paras.append(p())
paras.append(p(run("—— 全文完 ——", italic=True, color="808080", sz=20)))

# ---- 组装 document.xml ----
body = "".join(paras)
document_xml = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<w:document xmlns:w="%s" xmlns:r="%s" xmlns:wp="%s" '
    'xmlns:a="%s" xmlns:pic="%s">'
    "<w:body>%s</w:body></w:document>"
    % (W_NS, R_NS, WP_NS, A_NS, PIC_NS, body)
)

rels_xml = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Relationships xmlns="%s">'
    '<Relationship Id="rId2" Type="%s/image" Target="media/image1.png"/>'
    '<Relationship Id="rId3" Type="%s/image" Target="media/image2.png"/>'
    "</Relationships>" % (PKG_NS, R_NS, R_NS)
)

root_rels = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Relationships xmlns="%s">'
    '<Relationship Id="rId1" Type="%s/officeDocument" Target="word/document.xml"/>'
    "</Relationships>" % (PKG_NS, R_NS)
)

content_types = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
    '<Default Extension="xml" ContentType="application/xml"/>'
    '<Default Extension="png" ContentType="image/png"/>'
    '<Override PartName="/word/document.xml" '
    'ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
    "</Types>"
)

out = "samples/demo.docx"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", content_types)
    z.writestr("_rels/.rels", root_rels)
    z.writestr("word/document.xml", document_xml)
    z.writestr("word/_rels/document.xml.rels", rels_xml)
    z.writestr("word/media/image1.png", img1)
    z.writestr("word/media/image2.png", img2)

print("生成完成:", out)
print("段落数:", len(paras), "| document.xml 字节:", len(document_xml))
