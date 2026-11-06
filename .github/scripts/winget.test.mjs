// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {installerMetadata, manifests, prepare, releaseVersion} from './winget.mjs';

const root = 'https://github.com/LeBenco/IconicFolders';
const installer = Buffer.from('test installer, not a real NSIS executable');
const hash = createHash('sha256').update(installer).digest('hex');
const url = `${root}/releases/download/v1.2.3/IconicFolders-1.2.3-x64-Setup.exe`;
const checksums = `${hash}  ${url.split('/').pop()}\n`;
const release = {
  tag_name: 'v1.2.3', draft: false, prerelease: false,
  assets: [
    {name: 'IconicFolders-1.2.3-x64-Setup.exe', browser_download_url: url, size: installer.length, digest: `sha256:${hash}`},
    {name: 'SHA256SUMS.txt', browser_download_url: `${root}/releases/download/v1.2.3/SHA256SUMS.txt`}
  ]
};

test('only stable tags with valid Windows version fields are accepted', () => {
  assert.equal(releaseVersion('v1.2.3'), '1.2.3');
  assert.equal(releaseVersion('v256.0.0'), '256.0.0');
  for (const tag of [undefined, '1.2.3', 'v01.2.3', 'v1.2.3-beta', 'v1.2.3.4', 'v65536.0.0', 'v1.2.3\n']) {
    assert.throws(() => releaseVersion(tag));
  }
});

test('published installer bytes must agree with SHA256SUMS and GitHub metadata', () => {
  assert.deepEqual(installerMetadata(release, 'v1.2.3', checksums.replaceAll('\n', '\r\n'), installer),
    {version: '1.2.3', url, hash: hash.toUpperCase()});
  assert.throws(() => installerMetadata({...release, draft: true}, 'v1.2.3', checksums, installer));
  assert.throws(() => installerMetadata({...release, prerelease: true}, 'v1.2.3', checksums, installer));
  assert.throws(() => installerMetadata({...release, tag_name: 'v1.2.4'}, 'v1.2.3', checksums, installer));
  assert.throws(() => installerMetadata(release, 'v1.2.3', checksums, Buffer.from('changed installer')));
  assert.throws(() => installerMetadata(release, 'v1.2.3', checksums.replace('IconicFolders-1.2.3-x64-Setup.exe', 'Other.exe'), installer));
  assert.throws(() => installerMetadata(release, 'v1.2.3', checksums.replace(hash, '0'.repeat(64)), installer));
  assert.throws(() => installerMetadata(release, 'v1.2.3', checksums + checksums, installer));
  assert.throws(() => installerMetadata(release, 'v1.2.3', 'invalid checksum record', installer));
  const changedAsset = {...release.assets[0], digest: `sha256:${'0'.repeat(64)}`};
  assert.throws(() => installerMetadata({...release, assets: [changedAsset]}, 'v1.2.3', checksums, installer));
});

test('both x64 installation scopes preserve NSIS silent and directory arguments', () => {
  const files = manifests({version: '1.2.3', url, hash: hash.toUpperCase()});
  assert.equal(Object.keys(files).length, 3);
  for (const [name, content] of Object.entries(files)) {
    const type = content.match(/^ManifestType: (\w+)$/m)[1];
    const schema = content.match(/^ManifestVersion: ([\d.]+)$/m)[1];
    assert.equal(content.split('\n')[0],
      `# yaml-language-server: $schema=https://aka.ms/winget-manifest.${type}.${schema}.schema.json`, name);
  }
  const manifest = files['Benco.IconicFolders.installer.yaml'];
  assert.match(files['Benco.IconicFolders.yaml'], /^PackageIdentifier: Benco\.IconicFolders$/m);
  assert.match(files['Benco.IconicFolders.locale.en-US.yaml'], /^Publisher: Benco$/m);
  for (const scope of ['user', 'machine']) assert.match(manifest, new RegExp(`Scope: ${scope}`));
  for (const mode of ['CurrentUser', 'AllUsers']) assert.ok(manifest.includes(`Custom: /${mode}`));
  assert.equal(manifest.match(/Architecture: x64/g).length, 2);
  assert.equal(manifest.match(/SilentWithProgress: \/S/g).length, 2);
  assert.equal(manifest.match(/InstallLocation: \/D=<INSTALLPATH>/g).length, 2);
  assert.ok(!manifest.includes('/DefaultFileManager'));
  const userInstaller = manifest.split('Scope: user')[1].split('  - Architecture:')[0];
  assert.doesNotMatch(userInstaller, /ElevationRequirement:/,
    'Per-user installation must also be permitted from an elevated terminal.');
  assert.match(manifest.split('Scope: machine')[1], /ElevationRequirement: elevationRequired/);
});

test('first publication generates all manifests from public release assets without sending API credentials', async t => {
  const output = await fs.mkdtemp(path.join(os.tmpdir(), 'iconicfolders-winget-'));
  t.after(() => fs.rm(output, {recursive: true, force: true}));
  const requests = [];
  const request = async (address, options) => {
    requests.push(address);
    if (address.includes('/releases/tags/')) return Response.json(release);
    if (address.includes('/contents/manifests/')) {
      assert.ok(address.endsWith('/manifests/b/Benco/IconicFolders/1.2.3'));
      return new Response('', {status: 404});
    }
    if (address.includes('/search/issues')) return Response.json({items: [], incomplete_results: false});
    assert.equal(options, undefined);
    return new Response(address === url ? installer : checksums);
  };
  assert.equal(await prepare('v1.2.3', output, request), null);
  assert.equal((await fs.readdir(output)).length, 3);
  assert.ok((await fs.readFile(path.join(output, 'Benco.IconicFolders.installer.yaml'), 'utf8')).includes(hash.toUpperCase()));
  assert.equal(requests.length, 5);
});

test('retries skip merged versions and open PRs; API failures never mean package absence', async () => {
  for (const merged of [true, false]) {
    const request = async address => {
      if (address.includes('/releases/tags/')) return Response.json(release);
      if (address.includes('/contents/manifests/')) return merged ? Response.json([]) : new Response('', {status: 404});
      if (address.includes('/search/issues')) return Response.json({items: [{title: 'New version: Benco.IconicFolders version 1.2.3', html_url: 'https://github.com/microsoft/winget-pkgs/pull/123'}]});
      assert.fail('A retry must not download or write manifests.');
    };
    assert.match(await prepare('v1.2.3', 'unused', request), /Already/);
  }
  await assert.rejects(prepare('v1.2.3', 'unused', async () => new Response('', {status: 403})), /403/);
});

test('an open PR for the previous publisher does not suppress the Benco submission', async t => {
  const output = await fs.mkdtemp(path.join(os.tmpdir(), 'iconicfolders-winget-'));
  t.after(() => fs.rm(output, {recursive: true, force: true}));
  const request = async address => {
    if (address.includes('/releases/tags/')) return Response.json(release);
    if (address.includes('/contents/manifests/')) return new Response('', {status: 404});
    if (address.includes('/search/issues')) {
      assert.ok(decodeURIComponent(address).includes('"Benco.IconicFolders"'));
      return Response.json({items: [{title: 'New version: LeBenco.IconicFolders version 1.2.3'}]});
    }
    return new Response(address === url ? installer : checksums);
  };
  assert.equal(await prepare('v1.2.3', output, request), null);
  assert.deepEqual((await fs.readdir(output)).sort(), [
    'Benco.IconicFolders.installer.yaml', 'Benco.IconicFolders.locale.en-US.yaml', 'Benco.IconicFolders.yaml'
  ]);
});
