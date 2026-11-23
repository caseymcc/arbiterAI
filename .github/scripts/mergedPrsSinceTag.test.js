// Tests for mergedPrsSinceTag.js against a throwaway git repository and a fake
// GitHub API. Run with: node .github/scripts/mergedPrsSinceTag.test.js
const assert = require('assert');
const { execFileSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const mergedPrsSinceTag = require('./mergedPrsSinceTag.js');

function git(cwd, ...args) {
  return execFileSync('git', args, { cwd, encoding: 'utf8' }).trim();
}

function makeRepo() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mergedPrs-'));
  git(dir, 'init', '-q', '-b', 'main');
  git(dir, 'config', 'user.email', 'test@example.com');
  git(dir, 'config', 'user.name', 'test');
  return dir;
}

function commit(dir, message) {
  fs.appendFileSync(path.join(dir, 'file.txt'), message + '\n');
  git(dir, 'add', 'file.txt');
  git(dir, 'commit', '-q', '-m', message);
  return git(dir, 'rev-parse', 'HEAD');
}

// prsByCommit: sha -> [pr]; prsByNumber: number -> pr
function fakeGithub(prsByCommit, prsByNumber) {
  return {
    rest: {
      repos: {
        listPullRequestsAssociatedWithCommit: async ({ commit_sha }) => ({ data: prsByCommit[commit_sha] || [] }),
      },
      pulls: {
        get: async ({ pull_number }) => {
          if (!prsByNumber[pull_number]) throw new Error('Not Found');
          return { data: prsByNumber[pull_number] };
        },
      },
    },
  };
}

function pr(number, title, mergedAt, base = 'main') {
  return {
    number,
    title,
    html_url: `https://github.com/o/r/pull/${number}`,
    merged_at: mergedAt,
    base: { ref: base },
  };
}

async function run(dir, github) {
  const outputs = {};
  const core = { setOutput: (k, v) => { outputs[k] = v; }, info: () => {} };
  const context = { repo: { owner: 'o', repo: 'r' }, ref: 'refs/heads/main' };
  const result = await mergedPrsSinceTag({ github, context, core, cwd: dir });
  return { result, outputs };
}

const tests = {
  // The push that ended in "chore: bump arbiterAI_config submodule": its head is a
  // direct commit, but it carries two squash-merged PRs since the last tag.
  async directHeadCommitWithMergedPrsReleases() {
    const dir = makeRepo();
    commit(dir, 'chore: bump version to v0.1.15 [skip ci]');
    git(dir, 'tag', 'v0.1.15');
    const a = commit(dir, 'Sherpa onnx audio provider (#25)');
    commit(dir, 'build: enable vibevoice TTS by default (#26)');
    commit(dir, 'chore: bump arbiterAI_config submodule');

    const github = fakeGithub(
      { [a]: [pr(25, 'Sherpa onnx audio provider', '2026-11-20T10:00:00Z')] },
      {
        25: pr(25, 'Sherpa onnx audio provider', '2026-11-20T10:00:00Z'),
        26: pr(26, 'build: enable vibevoice TTS by default', '2026-11-21T10:00:00Z'),
      });
    const { result, outputs } = await run(dir, github);

    assert.strictEqual(outputs.should_release, 'true');
    assert.deepStrictEqual(result.prs.map(p => p.number), [25, 26]);
    assert.strictEqual(outputs.pr_count, '2');
    assert.match(outputs.pr_list, /#25: Sherpa onnx audio provider/);
    assert.match(outputs.pr_list, /#26: build: enable vibevoice TTS by default/);
    assert.strictEqual(outputs.last_tag, 'v0.1.15');
  },

  async noMergedPrsDoesNotRelease() {
    const dir = makeRepo();
    commit(dir, 'chore: bump version to v0.1.15 [skip ci]');
    git(dir, 'tag', 'v0.1.15');
    commit(dir, 'fix typo directly on main');
    // A message that names a PR which was closed without merging.
    commit(dir, 'revert the experiment from (#30)');

    const github = fakeGithub({}, { 30: pr(30, 'experiment', null) });
    const { result, outputs } = await run(dir, github);

    assert.strictEqual(outputs.should_release, 'false');
    assert.deepStrictEqual(result.prs, []);
  },

  // The bot pushes its bump commit and tags it, so nothing lies between the tag
  // and the head (the workflow also skips the bot's pushes by actor).
  async botBumpCommitDoesNotRelease() {
    const dir = makeRepo();
    commit(dir, 'Add feature (#24)');
    const bump = commit(dir, 'chore: bump version to v0.1.15 [skip ci]');
    git(dir, 'tag', 'v0.1.15');

    const github = fakeGithub({ [bump]: [pr(24, 'Add feature', '2026-11-19T10:00:00Z')] },
      { 24: pr(24, 'Add feature', '2026-11-19T10:00:00Z') });
    const { outputs } = await run(dir, github);

    assert.strictEqual(outputs.should_release, 'false');
  },

  // A PR's own commits all map to it; it is listed once. Merged PRs into other
  // branches and open PRs that contain a commit are not listed.
  async listsEachMergedPrOnceAndOnlyForThisBranch() {
    const dir = makeRepo();
    commit(dir, 'base');
    git(dir, 'tag', 'v0.1.0');
    const c1 = commit(dir, 'work on feature');
    const c2 = commit(dir, 'more work on feature');
    const merged = pr(40, 'Feature', '2026-11-22T10:00:00Z');
    const github = fakeGithub({
      [c1]: [merged, pr(41, 'Open PR containing it', null)],
      [c2]: [merged, pr(42, 'Merged into a release branch', '2026-11-22T11:00:00Z', 'release/0.1')],
    }, {});
    const { result } = await run(dir, github);

    assert.deepStrictEqual(result.prs.map(p => p.number), [40]);
  },

  // A squash merge puts its own number last: 'Revert "Feature (#12)" (#15)' is #15.
  // The release workflow tags with -a, so the range starts at an annotated tag.
  async squashSubjectUsesLastPrNumber() {
    const dir = makeRepo();
    commit(dir, 'base');
    git(dir, 'tag', '-a', 'v0.1.0', '-m', 'v0.1.0');
    commit(dir, 'Revert "Feature (#12)" (#15)');
    const github = fakeGithub({}, {
      12: pr(12, 'Feature', '2026-11-20T10:00:00Z'),
      15: pr(15, 'Revert "Feature (#12)"', '2026-11-22T10:00:00Z'),
    });
    const { result, outputs } = await run(dir, github);

    assert.deepStrictEqual(result.prs.map(p => p.number), [15]);
    assert.strictEqual(outputs.last_tag, 'v0.1.0');
  },
};

(async () => {
  let failed = 0;
  for (const [name, test] of Object.entries(tests)) {
    try {
      await test();
      console.log(`ok   ${name}`);
    } catch (e) {
      failed++;
      console.log(`FAIL ${name}\n     ${e.message.split('\n').join('\n     ')}`);
    }
  }
  if (failed) {
    console.log(`${failed} failed`);
    process.exit(1);
  }
  console.log('all passed');
})();
