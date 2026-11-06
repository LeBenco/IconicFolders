// SPDX-License-Identifier: GPL-3.0-only; Copyright (C) 2026 Benco
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {distributionFromMessage, publicationForTag, selectDistribution, publicationFromRelease} from './release-distribution.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
await fs.mkdir(path.join(root, 'build'), {recursive: true});
const repository = 'LeBenco/IconicFolders';
test('tag annotations default to none and select an explicit cumulative set', () => {
  assert.deepEqual(distributionFromMessage('IconicFolders 1.3.3\n'), []);
  assert.deepEqual(distributionFromMessage('IconicFolders-Distribution: none\n'), []);
  assert.deepEqual(distributionFromMessage('IconicFolders-Distribution: winget\n'), ['winget']);
  assert.deepEqual(distributionFromMessage('IconicFolders 1.3.3\r\n\r\nIconicFolders-Distribution: chocolatey,winget,chocolatey\r\n'), ['chocolatey', 'winget']);
  for (const value of ['', 'both', 'all', 'scoop', 'none,winget', 'winget,', 'winget\nIconicFolders-Distribution: chocolatey']) {
    assert.throws(() => distributionFromMessage(`IconicFolders-Distribution: ${value}\n`));
  }
});

test('automatic jobs respect only selected channels; manual jobs can submit a GitHub-only release', () => {
  const publication = publicationForTag('IconicFolders-Distribution: winget', 'v1.3.3', repository);
  assert.deepEqual(selectDistribution(publication, 'winget', repository), {tag: 'v1.3.3', enabled: true});
  assert.equal(selectDistribution(publication, 'chocolatey', repository).enabled, false);
  const legacy = {tag: 'v1.3.2', repository};
  assert.equal(selectDistribution(legacy, 'winget', repository).enabled, false);
  assert.equal(selectDistribution(legacy, 'chocolatey', repository).enabled, false);
  assert.deepEqual(selectDistribution(null, 'chocolatey', repository, 'v1.3.2'), {tag: 'v1.3.2', enabled: true});
  for (const invalid of [{...publication, repository: 'someone/else'}, {...publication, distribution: 'winget'},
    {...publication, distribution: ['none']}, {...publication, tag: 'v1.3.3\n'}, {...publication, tag: 'v1.3.3-rc1'}]) {
    assert.throws(() => selectDistribution(invalid, 'winget', repository));
  }
  assert.throws(() => selectDistribution(null, 'winget', repository, 'v1.3.2\n'));
});

test('the workflow CLI records the tag selection and exposes explicit manual selection without an artifact', async t => {
  const directory = await fs.mkdtemp(path.join(root, 'build/distribution-cli-'));
  t.after(() => fs.rm(directory, {recursive: true, force: true}));
  const message = path.join(directory, 'tag.txt'), publication = path.join(directory, 'publication.json');
  const output = path.join(directory, 'output');
  await fs.writeFile(message, 'IconicFolders 1.3.3\n\nIconicFolders-Distribution: winget,chocolatey\n');
  const script = fileURLToPath(new URL('release-distribution.mjs', import.meta.url));
  const env = {...process.env, RELEASE_TAG: 'v1.3.3', GITHUB_REPOSITORY: repository, GITHUB_OUTPUT: output};
  execFileSync(process.execPath, [script, 'record', message, publication], {env});
  assert.deepEqual(JSON.parse(await fs.readFile(publication, 'utf8')).distribution, ['winget', 'chocolatey']);
  execFileSync(process.execPath, [script, 'select', publication], {env: {...env, GITHUB_EVENT_NAME: 'workflow_run', DISTRIBUTION_CHANNEL: 'winget'}});
  assert.equal(await fs.readFile(output, 'utf8'), 'tag=v1.3.3\nenabled=true\n');
  await fs.writeFile(output, '');
  execFileSync(process.execPath, [script, 'select', path.join(directory, 'missing.json')], {env: {...env, GITHUB_EVENT_NAME: 'workflow_dispatch', DISTRIBUTION_CHANNEL: 'chocolatey'}});
  assert.equal(await fs.readFile(output, 'utf8'), 'tag=v1.3.3\nenabled=true\n');
});


test('release events read opt-in tag annotations and never submit unselected releases', async () => {
  const event = {action: 'published', repository: {full_name: repository},
    release: {tag_name: 'v1.1.0', draft: false, prerelease: false}};
  const sha = 'a'.repeat(40);
  const requested = [];
  const request = async url => {
    requested.push(url);
    return Response.json(url.endsWith('/git/ref/tags/v1.1.0')
      ? {object: {type: 'tag', sha}}
      : {tag: 'v1.1.0', message: 'IconicFolders 1.1.0\n\nIconicFolders-Distribution: winget,chocolatey\n'});
  };
  const publication = await publicationFromRelease(event, repository, request);
  assert.deepEqual(publication.distribution, ['winget', 'chocolatey']);
  assert.equal(selectDistribution(publication, 'winget', repository).enabled, true);
  assert.equal(selectDistribution(publication, 'chocolatey', repository).enabled, true);
  assert.equal(requested.length, 2);
  assert.ok(requested.every(url => url.startsWith(`https://api.github.com/repos/${repository}/git/`)));
  const lightweight = await publicationFromRelease(event, repository,
    async () => Response.json({object: {type: 'commit', sha}}));
  assert.deepEqual(lightweight.distribution, []);
  assert.equal(selectDistribution(lightweight, 'winget', repository).enabled, false);
  for (const flag of ['draft', 'prerelease']) {
    assert.equal(await publicationFromRelease({...event, release: {...event.release, [flag]: true}},
      repository, async () => assert.fail('Unpublished releases must not query tags.')), null);
  }
  await assert.rejects(publicationFromRelease({...event, repository: {full_name: 'someone/else'}}, repository), /Invalid/);
  await assert.rejects(publicationFromRelease(event, repository, async () => new Response('', {status: 403})), /403/);
  await assert.rejects(publicationFromRelease(event, repository, async () => Response.json({object: {type: 'blob', sha}})), /Invalid/);
  await assert.rejects(publicationFromRelease(event, repository, async url => Response.json(url.includes('/git/ref/')
    ? {object: {type: 'tag', sha}} : {tag: 'v1.1.1', message: 'IconicFolders-Distribution: winget'})), /mismatch/);
});
