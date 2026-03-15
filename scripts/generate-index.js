#!/usr/bin/env node
/**
 * Generates index.json from project.json files found in src/ subdirectories.
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

    // Check if source files exist
    const srcPath = path.join(projectDir, 'src');
    const hasSrc = fs.existsSync(srcPath) && fs.statSync(srcPath).isDirectory();

    // Check if docs exist
    const docsPath = path.join(projectDir, 'docs');
    const hasDocs = fs.existsSync(docsPath);

    projects.push({
      id: meta.id,
      name: meta.name,
      description: meta.description ?? '',
      softwarePlatform: meta.softwarePlatform ?? null,
      soc: meta.soc ?? null,
      fqbn: meta.fqbn ?? undefined,
      version: meta.version ?? '1.0.0',
      tags: meta.tags ?? [],
      path: `src/${dir}`,
      hasSrc,
      hasDocs,
    });

    console.log(`  [ok]   ${meta.id} (${meta.softwarePlatform ?? 'hardware'}, ${meta.soc ?? 'no soc'})`);
  } catch (e) {
    console.warn(`  [err]  ${dir} — ${e.message}`);
  }
}

const index = {
  version: '1',
  updatedAt: new Date().toISOString().split('T')[0],
  repoUrl: 'https://github.com/platform-minis/MinisProjects',
  projects,
};

fs.writeFileSync(OUTPUT, JSON.stringify(index, null, 2) + '\n');
console.log(`\nGenerated index.json with ${projects.length} projects.`);
