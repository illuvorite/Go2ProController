#!/usr/bin/env node
// ============================================================================
// 网页界面自检（免构建那套：importmap + 原生 ESM）。
//
// 为什么需要它：client/assets/web 是**免构建**的（没有打包器帮忙发现错误），
// 一个 import 路径写错 / 一个 .js 语法错，只有真打开浏览器才会发现，
// 而在 WSL/CI 里连浏览器都没有。这个脚本把"打开浏览器才能发现的问题"提前到命令行：
//
//   1. 每个 .js 语法可解析（写成临时 .mjs 后交给 node --check）；
//   2. 每个 `import ... from '...'` 的目标文件真的存在；
//   3. index.html / style.css / 各组件里引用的静态资源（src/href/url()/@import）存在；
//   4. 关键文件在位（index.html、app.js、vendor 的 vue 构建）。
//
// 用法：node scripts/web_check.mjs        （在仓库根或任意目录都能跑）
// ============================================================================

import { execFileSync } from 'node:child_process'
import { cpSync, existsSync, mkdtempSync, readdirSync, readFileSync, rmSync, statSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = dirname(fileURLToPath(import.meta.url))
const WEB = resolve(HERE, '..', 'client', 'assets', 'web')

let checks = 0
let failures = 0

function ok(msg) {
  checks++
  console.log('  ok   ' + msg)
}

function bad(msg) {
  checks++
  failures++
  console.log('  FAIL ' + msg)
}

function walk(dir, out = []) {
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    if (e.name === 'node_modules' || e.name.startsWith('.')) continue
    const p = join(dir, e.name)
    if (e.isDirectory()) walk(p, out)
    else out.push(p)
  }
  return out
}

if (!existsSync(WEB)) {
  console.error('找不到网页目录: ' + WEB)
  process.exit(2)
}

const files = walk(WEB)
const rel = (p) => relative(WEB, p).replace(/\\/g, '/')

console.log('== 1) 关键文件在位 ==')
for (const need of ['index.html', 'app.js', 'store.js', 'api.js', 'motion.js', 'icons.js',
                    'style.css', 'vendor/vue.esm-browser.prod.js']) {
  if (existsSync(join(WEB, need))) ok(need)
  else bad('缺少 ' + need)
}

console.log('== 2) .js 语法检查（node --check）==')
const tmp = mkdtempSync(join(tmpdir(), 'go2web-'))
try {
  const js = files.filter((f) => f.endsWith('.js'))
  for (const f of js) {
    const target = join(tmp, rel(f).replace(/\//g, '__'))
    cpSync(f, target)
    try {
      execFileSync(process.execPath, ['--check', target], { stdio: 'pipe' })
      ok('语法 ' + rel(f))
    } catch (e) {
      bad('语法 ' + rel(f) + '\n' + String(e.stderr || e.message).split('\n').slice(0, 4).join('\n'))
    }
  }
} finally {
  rmSync(tmp, { recursive: true, force: true })
}

// 把 "./x" / "../x" 这类相对引用解析到真实文件（允许省略 .js 后缀与目录 index）
//
// ⚠ 基准要看引用种类（这是本脚本最容易搞错的地方）：
//   · `import ... from './x'`  → 相对**模块文件自身**（所以 components/ 里写 ../store.js）
//   · 模板里的 src=/href=、CSS 的 url() → 相对**文档根**（浏览器行为：index.html 在 /，
//     所以 components/band.js 里的 "./img/go2.jpg" 指的是 web 根下的 img/go2.jpg）
function resolves(base, spec) {
  if (/^(https?:)?\/\//.test(spec) || spec.startsWith('data:')) return true
  if (spec.startsWith('/')) return existsSync(join(WEB, spec.replace(/^\//, '')))
  const p = resolve(dirname(base), spec)
  if (existsSync(p) && statSync(p).isFile()) return true
  if (existsSync(p + '.js')) return true
  if (existsSync(join(p, 'index.js'))) return true
  return false
}

// 文档根基准（模板 / CSS 里的相对引用都用它）
const DOC_BASE = join(WEB, 'index.html')

/// 是否是"裸模块名"（如 'vue'）—— 这种靠 index.html 里的 importmap 解析，
/// 不是文件路径，所以不能用文件存在性去判断，要查 importmap。
function isBare(spec) {
  return !spec.startsWith('.') && !spec.startsWith('/') && !/^[a-z]+:/i.test(spec)
}

// ---- importmap：裸模块名必须在 index.html 里声明过 ----
console.log('== 3a) index.html 的 importmap ==')
const importMapSpecs = new Set()
{
  const html = readFileSync(join(WEB, 'index.html'), 'utf8')
  const m = html.match(/<script[^>]*type\s*=\s*["']importmap["'][^>]*>([\s\S]*?)<\/script>/i)
  if (!m) {
    bad('index.html 里没有 importmap（免构建方案必须靠它解析 vue）')
  } else {
    try {
      const map = JSON.parse(m[1])
      for (const k of Object.keys(map.imports || {})) importMapSpecs.add(k)
      ok('importmap 解析成功，映射 ' + importMapSpecs.size + ' 项: ' +
         [...importMapSpecs].join(', '))
    } catch (e) {
      bad('importmap 不是合法 JSON: ' + e.message)
    }
  }
}

console.log('== 3) import 路径可解析 ==')
for (const f of files.filter((f) => f.endsWith('.js'))) {
  const src = readFileSync(f, 'utf8')
  const re = /(?:import|export)[^'"\n]*?from\s*['"]([^'"]+)['"]|import\s*\(\s*['"]([^'"]+)['"]\s*\)|import\s*['"]([^'"]+)['"]/g
  let m
  while ((m = re.exec(src))) {
    const spec = m[1] || m[2] || m[3]
    if (!spec) continue
    if (isBare(spec)) {
      if (importMapSpecs.has(spec)) ok(rel(f) + ' → ' + spec + '（importmap）')
      else bad(rel(f) + ' → ' + spec + '（裸模块名未在 importmap 里声明）')
    } else if (resolves(f, spec)) {
      ok(rel(f) + ' → ' + spec)
    } else {
      bad(rel(f) + ' → ' + spec + '（找不到目标文件）')
    }
  }
}

console.log('== 4) 静态资源引用可解析（html/css/js 模板串）==')
for (const f of files.filter((f) => /\.(html|css|js)$/.test(f))) {
  const src = readFileSync(f, 'utf8')
  const specs = []
  // HTML / Vue 组件模板里的 src="..." href="..."（js 组件也含模板字符串）
  for (const m of src.matchAll(/\b(?:src|href)\s*=\s*["']([^"']+)["']/g)) specs.push(m[1])
  for (const m of src.matchAll(/"(\.[^"]*\.js)"/g)) specs.push(m[1])
  // CSS: url(...) 与 @import "..."
  for (const m of src.matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)) specs.push(m[1])
  for (const m of src.matchAll(/@import\s+(?:url\()?\s*['"]([^'"]+)['"]/g)) specs.push(m[1])
  for (const spec of specs) {
    if (spec.startsWith('#') || spec.startsWith('data:')) continue
    // 模板 / CSS 里的引用按文档根解析（见 resolves() 的注释）
    if (resolves(DOC_BASE, spec)) ok(rel(f) + ' → ' + spec)
    else bad(rel(f) + ' → ' + spec + '（找不到目标文件，按文档根解析）')
  }
}

console.log('')
console.log(`web_check: ${checks} 项检查, ${failures} 项失败  → ${failures === 0 ? 'PASS' : 'FAIL'}`)
process.exit(failures === 0 ? 0 : 1)
