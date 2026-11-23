// Finds the pull requests merged into the pushed branch since the last v* tag.
// Used by release-point.yml through actions/github-script:
//   await require('./.github/scripts/mergedPrsSinceTag.js')({github, context, core})
//
// Sets the outputs:
//   should_release  "true" when at least one merged PR is found
//   pr_count        number of PRs
//   pr_list         markdown list, one "- #N: title (url)" line per PR, oldest merge first
//   last_tag        the tag the range starts from ("" if there is none)
// and returns {prs, lastTag}.
const { execFileSync } = require('child_process');

function git(cwd, args) {
  return execFileSync('git', args, { cwd, encoding: 'utf8' }).trim();
}

function lastReleaseTag(cwd) {
  try {
    return git(cwd, ['describe', '--tags', '--match', 'v*', '--abbrev=0', 'HEAD']);
  } catch (e) {
    return '';
  }
}

module.exports = async ({ github, context, core, cwd = process.cwd() }) => {
  const { owner, repo } = context.repo;
  const branch = context.ref.replace(/^refs\/heads\//, '');
  const lastTag = lastReleaseTag(cwd);
  const range = lastTag ? `${lastTag}..HEAD` : 'HEAD';

  const log = git(cwd, ['log', '--format=%H%x00%s', range]);
  const commits = log ? log.split('\n').map(line => {
    const [sha, subject] = line.split('\0');
    return { sha, subject };
  }) : [];
  core.info(`${commits.length} commit(s) since ${lastTag || 'the start of history'} on ${branch}`);

  const prs = new Map();
  const isMergedHere = pr => pr.merged_at !== null && pr.base && pr.base.ref === branch;

  for (const { sha, subject } of commits) {
    // Merge commits and the commits of a merged PR are associated with it.
    const assoc = await github.rest.repos.listPullRequestsAssociatedWithCommit({ owner, repo, commit_sha: sha });
    let found = assoc.data.filter(isMergedHere);

    // A squash merge's commit ends in "(#N)" and the association can lag behind it.
    // Only the PR whose merge commit this is counts, not one a subject merely names.
    if (found.length === 0) {
      const match = [...subject.matchAll(/\(#(\d+)\)/g)].pop();
      if (match) {
        try {
          const { data } = await github.rest.pulls.get({ owner, repo, pull_number: parseInt(match[1], 10) });
          if (isMergedHere(data) && data.merge_commit_sha === sha) {
            found = [data];
          }
        } catch (e) {
          core.info(`Could not fetch PR #${match[1]}: ${e.message}`);
        }
      }
    }

    for (const pr of found) {
      prs.set(pr.number, pr);
    }
  }

  const sorted = [...prs.values()].sort((a, b) => new Date(a.merged_at) - new Date(b.merged_at));
  const list = sorted.map(pr => `- #${pr.number}: ${pr.title} (${pr.html_url})`).join('\n');

  core.setOutput('should_release', sorted.length > 0 ? 'true' : 'false');
  core.setOutput('pr_count', String(sorted.length));
  core.setOutput('pr_list', list);
  core.setOutput('last_tag', lastTag);
  core.info(sorted.length > 0
    ? `Merged PRs since ${lastTag || 'the start'}:\n${list}`
    : 'No merged PRs since the last release tag: skipping release.');

  return { prs: sorted, lastTag };
};
