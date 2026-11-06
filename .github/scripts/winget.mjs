// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
import {createHash} from 'node:crypto';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

const packageId = 'Benco.IconicFolders';
const repository = 'LeBenco/IconicFolders';
const projectUrl = `https://github.com/${repository}`;

export function releaseVersion(tag) {
  if (!/^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(tag) || tag !== tag.trim()) {
    throw new Error('WinGet requires a stable vX.Y.Z release tag.');
  }
  const version = tag.slice(1);
  if (version.split('.').some(part => Number(part) > 65535)) {
    throw new Error('Release version components must fit in a Windows version field.');
  }
  return version;
}

export function installerMetadata(release, tag, checksums, installer) {
  const version = releaseVersion(tag);
  if (release.draft || release.prerelease || release.tag_name !== tag) {
    throw new Error('Only a published stable release can be submitted.');
  }
  const name = `IconicFolders-${version}-x64-Setup.exe`;
  const url = `${projectUrl}/releases/download/${tag}/${name}`;
  const asset = release.assets.find(item => item.name === name);
  if (!asset || asset.browser_download_url !== url || asset.size !== installer.length) {
    throw new Error('The published installer is missing or its size/URL does not match.');
  }
  const hash = createHash('sha256').update(installer).digest('hex');
  const entries = checksums.trim().split(/\r?\n/).map(line => {
    const match = /^([0-9a-f]{64})[ \t]+\*?([^\r\n]+)$/i.exec(line);
    if (!match) throw new Error('Invalid SHA256SUMS.txt record.');
    return {hash: match[1].toLowerCase(), name: match[2]};
  }).filter(entry => entry.name === name);
  if (entries.length !== 1 || entries[0].hash !== hash) {
    throw new Error('The installer does not match SHA256SUMS.txt.');
  }
  if (asset.digest && asset.digest.toLowerCase() !== `sha256:${hash}`) {
    throw new Error('The installer does not match the GitHub asset digest.');
  }
  return {version, url, hash: hash.toUpperCase()};
}

export function manifests({version, url, hash}) {
  const common = `PackageIdentifier: ${packageId}\nPackageVersion: ${version}\n`;
  const header = type => `# yaml-language-server: $schema=https://aka.ms/winget-manifest.${type}.1.9.0.schema.json\n\n`;
  const footer = type => `ManifestType: ${type}\nManifestVersion: 1.9.0\n`;
  return {
    [`${packageId}.yaml`]: header('version') + common + 'DefaultLocale: en-US\n' + footer('version'),
    [`${packageId}.locale.en-US.yaml`]: header('defaultLocale') + common + `PackageLocale: en-US
Publisher: Benco
PublisherUrl: ${projectUrl}
PublisherSupportUrl: ${projectUrl}/issues
PackageName: IconicFolders
PackageUrl: ${projectUrl}
License: GPL-3.0-only
LicenseUrl: ${projectUrl}/blob/v${version}/LICENSE
ShortDescription: Change folder icons from the Windows context menu.
Moniker: iconicfolders
Tags:
  - folder-icons
  - customization
ReleaseNotesUrl: ${projectUrl}/releases/tag/v${version}
` + footer('defaultLocale'),
    [`${packageId}.installer.yaml`]: header('installer') + common + `InstallerType: nullsoft
MinimumOSVersion: 10.0.0.0
UpgradeBehavior: install
ProductCode: IconicFolders
AppsAndFeaturesEntries:
  - DisplayName: IconicFolders
    ProductCode: IconicFolders
Installers:
  - Architecture: x64
    Scope: user
    InstallerUrl: ${url}
    InstallerSha256: ${hash}
    InstallerSwitches:
      Silent: /S
      SilentWithProgress: /S
      Custom: /CurrentUser
      InstallLocation: /D=<INSTALLPATH>
  - Architecture: x64
    Scope: machine
    ElevationRequirement: elevationRequired
    InstallerUrl: ${url}
    InstallerSha256: ${hash}
    InstallerSwitches:
      Silent: /S
      SilentWithProgress: /S
      Custom: /AllUsers
      InstallLocation: /D=<INSTALLPATH>
` + footer('installer')
  };
}

export async function prepare(tag, output, request = fetch) {
  const version = releaseVersion(tag);
  const headers = {'Accept': 'application/vnd.github+json'};
  if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
  async function api(endpoint, allowMissing = false) {
    const response = await request(`https://api.github.com/${endpoint}`, {headers});
    if (allowMissing && response.status === 404) return null;
    if (!response.ok) throw new Error(`GitHub API failed (${response.status}): ${endpoint}`);
    return response.json();
  }
  const release = await api(`repos/${repository}/releases/tags/${tag}`);
  // Check eligibility before treating an existing submission as successful.
  if (release.draft || release.prerelease || release.tag_name !== tag) {
    throw new Error('Only a published stable release can be submitted.');
  }
  const existing = await api(`repos/microsoft/winget-pkgs/contents/manifests/b/Benco/IconicFolders/${version}`, true);
  if (existing) return 'Already present in winget-pkgs.';
  const query = `repo:microsoft/winget-pkgs is:pr is:open in:title "${packageId}" "${version}"`;
  const pulls = await api(`search/issues?q=${encodeURIComponent(query)}&per_page=100`);
  if (pulls.incomplete_results) throw new Error('GitHub returned incomplete PR search results.');
  const packagePattern = new RegExp(`(?:^|\\s)${packageId.replaceAll('.', '\\.')}(?=\\s|$)`);
  const versionPattern = new RegExp(`(?:^|\\s)${version.replaceAll('.', '\\.')}(?=\\s|$)`);
  const existingPull = pulls.items.find(item => packagePattern.test(item.title) && versionPattern.test(item.title));
  if (existingPull) return `Already submitted: ${existingPull.html_url}`;
  async function download(name) {
    const asset = release.assets.find(item => item.name === name);
    const expected = `${projectUrl}/releases/download/${tag}/${name}`;
    if (!asset || asset.browser_download_url !== expected) throw new Error(`Missing release asset: ${name}`);
    // Release assets are public; never forward the API token to a download host.
    const response = await request(expected);
    if (!response.ok) throw new Error(`Release download failed (${response.status}): ${name}`);
    return Buffer.from(await response.arrayBuffer());
  }
  const installer = await download(`IconicFolders-${version}-x64-Setup.exe`);
  const checksums = await download('SHA256SUMS.txt');
  const metadata = installerMetadata(release, tag, checksums.toString('utf8'), installer);
  await fs.mkdir(output, {recursive: true});
  for (const [name, content] of Object.entries(manifests(metadata))) {
    await fs.writeFile(path.join(output, name), content, {flag: 'wx'});
  }
  return null;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const output = path.resolve('build/winget');
  const skipped = await prepare(process.env.RELEASE_TAG, output);
  const message = skipped || `Prepared ${packageId} ${process.env.RELEASE_TAG} from verified release assets.`;
  console.log(message);
  if (process.env.GITHUB_OUTPUT) await fs.appendFile(process.env.GITHUB_OUTPUT, `submit=${!skipped}\n`);
  if (process.env.GITHUB_STEP_SUMMARY) await fs.appendFile(process.env.GITHUB_STEP_SUMMARY, `${message}\n`);
}
