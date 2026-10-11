// 语法自检：把每个前端 js 去掉 import/export 后交给 vm.Script 解析。
// import 在浏览器里由 importmap 解析、在 Node 里不解析 —— 这里只关心**语法**，
// 用来在打开预览前抓出拼写/括号类错误（比开浏览器快得多）。
const fs = require('fs')
const path = require('path')
const vm = require('vm')

const root = process.argv[2] || '.'
const files = []
;(function walk(d) {
  for (const e of fs.readdirSync(d, { withFileTypes: true })) {
    const p = path.join(d, e.name)
    if (e.isDirectory()) {
      if (e.name !== 'vendor') walk(p)
    } else if (e.name.endsWith('.js')) files.push(p)
  }
})(root)

let bad = 0
for (const f of files) {
  let s = fs.readFileSync(f, 'utf8')
  s = s.replace(/^\s*import[\s\S]*?from\s+['"][^'"]+['"]\s*;?\s*$/gm, '')
  s = s.replace(/^\s*import\s+['"][^'"]+['"]\s*;?\s*$/gm, '')
  // export default X  → const __default = X（否则 "export " 一去掉就剩裸 default，语法非法）
  s = s.replace(/\bexport\s+default\b/g, 'const __default =')
  s = s.replace(/\bexport\s+/g, '')
  try {
    new vm.Script(s, { filename: f })
  } catch (e) {
    bad++
    console.log('SYNTAX ERROR', f, e.message)
  }
}
console.log(bad === 0 ? `全部 ${files.length} 个 js 语法 OK` : `有 ${bad} 个语法错误`)
process.exit(bad === 0 ? 0 : 1)
