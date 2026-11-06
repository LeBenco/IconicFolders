// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
import fs from 'node:fs/promises';
import {pathToFileURL} from 'node:url';

const channels = ['winget', 'chocolatey'];
function checkTag(tag) {
  if (typeof tag !== 'string' || tag !== tag.trim() || !/^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(tag)) {
    throw new Error('Expected a stable vX.Y.Z release tag.');
  }
}

export function distributionFromMessage(message) {
  const fields = [...message.replaceAll('\r\n', '\n').matchAll(/^IconicFolders-Distribution:[ \t]*(.*)$/gm)];
  if (!fields.length) return [];
  if (fields.length !== 1) throw new Error('Duplicate IconicFolders-Distribution fields.');
  const value = fields[0][1].trim();
  if (value === 'none') return [];
  const selected = value.split(',').map(channel => channel.trim());
  if (selected.some(channel => !channels.includes(channel))) {
    throw new Error('Expected winget, chocolatey or exclusive none in IconicFolders-Distribution.');
  }
  return [...new Set(selected)];
}

export function publicationForTag(message, tag, repository) {
  checkTag(tag);
  return {tag, repository, distribution: distributionFromMessage(message)};
}

export function selectDistribution(publication, channel, repository, manualTag = null) {
  if (!channels.includes(channel)) throw new Error('Unknown distribution channel.');
  // A manual dispatch is an explicit request, including for older GitHub-only releases.
  if (manualTag !== null) {
    checkTag(manualTag);
    return {tag: manualTag, enabled: true};
  }
  if (publication?.repository !== repository) throw new Error('Invalid release publication repository.');
  checkTag(publication.tag);
  const selected = publication.distribution ?? [];
  if (!Array.isArray(selected) || selected.some(item => !channels.includes(item))) {
    throw new Error('Invalid release publication distribution list.');
  }
  return {tag: publication.tag, enabled: selected.includes(channel)};
}

// Read the opt-in channels from the tag when no parent publication workflow exists.
export async function publicationFromRelease(event, repository, request = fetch) {
  if (event?.action !== 'published' || event.repository?.full_name !== repository) {
    throw new Error('Invalid release publication event.');
  }
  const release = event.release;
  if (!release) throw new Error('Missing release publication.');
  if (release.draft || release.prerelease) return null;
  checkTag(release.tag_name);
  const headers = {Accept: 'application/vnd.github+json'};
  if (process.env.GH_TOKEN) headers.Authorization = `Bearer ${process.env.GH_TOKEN}`;
  async function api(endpoint) {
    const response = await request(`https://api.github.com/repos/${repository}/${endpoint}`, {headers});
    if (!response.ok) throw new Error(`GitHub tag lookup failed (${response.status}).`);
    return response.json();
  }
  const ref = await api(`git/ref/tags/${release.tag_name}`);
  if (ref.object?.type === 'commit') return publicationForTag('', release.tag_name, repository);
  if (ref.object?.type !== 'tag' || !/^[a-f0-9]{40}$/i.test(ref.object.sha)) {
    throw new Error('Invalid annotated release tag.');
  }
  const tag = await api(`git/tags/${ref.object.sha}`);
  if (tag.tag !== release.tag_name) throw new Error('Release tag mismatch.');
  return publicationForTag(tag.message, release.tag_name, repository);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const [mode, input, output] = process.argv.slice(2);
  if (mode === 'record') {
    const publication = publicationForTag(await fs.readFile(input, 'utf8'), process.env.RELEASE_TAG, process.env.GITHUB_REPOSITORY);
    await fs.writeFile(output, JSON.stringify(publication, null, 2) + '\n');
    console.log(`Distribution: ${publication.distribution.join(', ') || 'none'}`);
  } else if (mode === 'select') {
    const manual = process.env.GITHUB_EVENT_NAME === 'workflow_dispatch';
    const releaseEvent = process.env.GITHUB_EVENT_NAME === 'release';
    const publication = manual ? null : releaseEvent
      ? await publicationFromRelease(JSON.parse(await fs.readFile(process.env.GITHUB_EVENT_PATH, 'utf8')), process.env.GITHUB_REPOSITORY)
      : JSON.parse(await fs.readFile(input, 'utf8'));
    const selection = releaseEvent && publication === null ? {tag: '', enabled: false}
      : selectDistribution(publication, process.env.DISTRIBUTION_CHANNEL, process.env.GITHUB_REPOSITORY, manual ? process.env.RELEASE_TAG : null);
    if (process.env.GITHUB_OUTPUT) await fs.appendFile(process.env.GITHUB_OUTPUT, `tag=${selection.tag}\nenabled=${selection.enabled}\n`);
    console.log(`${selection.enabled ? 'Selected' : 'Skipped'} ${process.env.DISTRIBUTION_CHANNEL} for ${selection.tag}.`);
  } else {
    throw new Error('Usage: release-distribution.mjs record <tag-message> <publication> | select <publication>');
  }
}
