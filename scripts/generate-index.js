#!/usr/bin/env node
/**
 * Generates index.json from project.json files found in src/ subdirectories
 * and modules from modules.json.
 * Usage: node scripts/generate-index.js
 */

const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const SRC_DIR = path.join(ROOT, 'src');
const OUTPUT = path.join(ROOT, 'index.json');

const projects = [];

for (const dir of fs.readdirSync(SRC_DIR).sort()) {
  const projectDir = path.join(SRC_DIR, dir);
  if (!fs.statSync(projectDir).isDirectory()) continue;

  const projectJsonPath = path.join(projectDir, 'project.json');
  if (!fs.existsSync(projectJsonPath)) {
    console.warn(`  [skip] ${dir} — no project.json`);
    continue;
  }

  try {
    const meta = JSON.parse(fs.readFileSync(projectJsonPath, 'utf8'));

    // Validate required fields
    if (!meta.id || !meta.name) {
      console.warn(`  [skip] ${dir} — project.json missing id or name`);
      continue;
    }

    // Check if source files exist and enumerate sketches
    // Arduino: src/{dir}/src/{sketchName}/  uPython: src/{dir}/sketches/{sketchName}/
    const srcPath = path.join(projectDir, 'src');
    const sketchesPath = path.join(projectDir, 'sketches');
    const hasSrc = (fs.existsSync(srcPath) && fs.statSync(srcPath).isDirectory())
                || (fs.existsSync(sketchesPath) && fs.statSync(sketchesPath).isDirectory());

    const sketches = [];
    const sketchScanDir = fs.existsSync(srcPath) ? srcPath : fs.existsSync(sketchesPath) ? sketchesPath : null;
    const sketchRelBase = fs.existsSync(srcPath) ? `src/${dir}/src` : `src/${dir}/sketches`;
    if (sketchScanDir) {
      for (const sketchDir of fs.readdirSync(sketchScanDir).sort()) {
        const sketchPath = path.join(sketchScanDir, sketchDir);
        if (fs.statSync(sketchPath).isDirectory()) {
          const sketchFiles = fs.readdirSync(sketchPath).map((f) => `${sketchRelBase}/${sketchDir}/${f}`);
          sketches.push({ name: sketchDir, files: sketchFiles });
        }
      }
    }

    // Check if docs exist and find first markdown file
    // Also check for README.md at project root
    const docsPath = path.join(projectDir, 'docs');
    const rootReadme = path.join(projectDir, 'README.md');
    const hasDocs = fs.existsSync(docsPath) || fs.existsSync(rootReadme);
    let readmePath = null;
    if (fs.existsSync(docsPath)) {
      const mdFiles = fs.readdirSync(docsPath).filter((f) => f.endsWith('.md'));
      if (mdFiles.length > 0) readmePath = `src/${dir}/docs/${mdFiles[0]}`;
    } else if (fs.existsSync(rootReadme)) {
      readmePath = `src/${dir}/README.md`;
    }

    projects.push({
      id: meta.id,
      name: meta.name,
      description: meta.description ?? '',
      softwarePlatform: meta.softwarePlatform ?? null,
      moduleId: meta.moduleId ?? null,
      version: meta.version ?? '1.0.0',
      tags: meta.tags ?? [],
      path: `src/${dir}`,
      hasSrc,
      hasDocs,
      sketches,
      readmePath,
    });

    console.log(`  [ok]   ${meta.id} (${meta.softwarePlatform ?? 'hardware'}, ${meta.moduleId ?? 'no module'})`);
  } catch (e) {
    console.warn(`  [err]  ${dir} — ${e.message}`);
  }
}

// Load modules
const modulesPath = path.join(ROOT, 'modules.json');
let modules = [];
if (fs.existsSync(modulesPath)) {
  try {
    const modulesData = JSON.parse(fs.readFileSync(modulesPath, 'utf8'));
    modules = modulesData.modules ?? [];
    console.log(`\nLoaded ${modules.length} modules from modules.json.`);
  } catch (e) {
    console.warn(`  [err]  modules.json — ${e.message}`);
  }
}

const index = {
  version: '1',
  updatedAt: new Date().toISOString().split('T')[0],
  repoUrl: 'https://github.com/platform-minis/MinisProjects',
  projects,
  modules,
};

fs.writeFileSync(OUTPUT, JSON.stringify(index, null, 2) + '\n');
console.log(`Generated index.json with ${projects.length} projects and ${modules.length} modules.`);
