// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {installerMetadata, releaseVersion} from './winget.mjs';

const project = 'https://github.com/LeBenco/IconicFolders';
const templates = new URL('../../installer/chocolatey/', import.meta.url);

export async function packageFiles(metadata) {
  const replacements = {'@VERSION@': metadata.version, '@INSTALLER_URL@': metadata.url,
    '@INSTALLER_SHA256@': metadata.hash};
  const files = {};
  for (const name of ['iconicfolders.nuspec', 'tools/chocolateyinstall.ps1', 'tools/chocolateyuninstall.ps1']) {
    let content = await fs.readFile(new URL(name, templates), 'utf8');
    for (const [key, value] of Object.entries(replacements)) content = content.replaceAll(key, value);
    files[name] = content;
  }
  return files;
}

export async function prepare(tag, output, request = fetch) {
  const version = releaseVersion(tag);
  const headers = {Accept: 'application/vnd.github+json'};
  if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
  const response = await request(`https://api.github.com/repos/LeBenco/IconicFolders/releases/tags/${tag}`, {headers});
  if (!response.ok) throw new Error(`GitHub release lookup failed (${response.status}).`);
  const release = await response.json();
  if (release.draft || release.prerelease || release.tag_name !== tag) throw new Error('A published stable release is required.');
  async function download(name) {
    const url = `${project}/releases/download/${tag}/${name}`;
    if (!release.assets.some(asset => asset.name === name && asset.browser_download_url === url)) {
      throw new Error(`Missing official release asset: ${name}`);
    }
    // The API credential must never be forwarded to public download hosts.
    const result = await request(url);
    if (!result.ok) throw new Error(`Release download failed (${result.status}): ${name}`);
    return Buffer.from(await result.arrayBuffer());
  }
  const installer = await download(`IconicFolders-${version}-x64-Setup.exe`);
  const checksums = await download('SHA256SUMS.txt');
  const metadata = installerMetadata(release, tag, checksums.toString('utf8'), installer);
  for (const [name, content] of Object.entries(await packageFiles(metadata))) {
    const target = path.join(output, name);
    await fs.mkdir(path.dirname(target), {recursive: true});
    await fs.writeFile(target, content, {flag: 'wx'});
  }
  return metadata;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const metadata = await prepare(process.env.RELEASE_TAG, path.resolve('build/chocolatey/iconicfolders'));
  if (process.env.GITHUB_OUTPUT) await fs.appendFile(process.env.GITHUB_OUTPUT, `version=${metadata.version}\n`);
  console.log(`Prepared iconicfolders ${metadata.version} from the verified published installer (${metadata.hash}).`);
}
