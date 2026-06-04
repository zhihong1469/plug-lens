# Contributing to plug-lens
[简体中文](CONTRIBUTING.md) | [English](CONTRIBUTING.en.md)

First off, thanks for taking the time to contribute! ❤️

All types of contributions are encouraged and valued. Please read through this document before submitting any contributions. It will greatly reduce maintainers' workload and bring a smoother collaboration experience for all participants.

## I Want To Contribute
### Reporting Bugs
- Submit bug reports via project [Issues](https://github.com/zhihong1469/plug-lens/issues)
- Provide complete reproduce steps including hardware platform, software version and operation flow, expected result and actual abnormal phenomenon
- Attach runtime logs, screenshots or screen recording whenever available for quick troubleshooting

### Suggesting Enhancements
- Propose new feature requests via GitHub Issues page
- Clarify practical usage scenarios and the practical value brought by your expected feature
- Technical implementation ideas are also welcome to attach together

### Your First Code Contribution
Issues tagged with `good first issue` are friendly for newcomers, which need no in-depth comprehension of the whole code repository:
- Optimize documents including README, source code comments and operation tutorials
- Supplement demo codes and user guides
- Fix typo, format defects and trivial bugs
- Complete cross-hardware verification and feedback test results

### Pull Requests
1. Fork this repository to your personal GitHub account
2. Cut your dev branch from corresponding target `release/*` branch with unified naming specification:
   - New feature: `git checkout -b feature/{platform}/feature-name`
   - Bug fix: `git checkout -b fix/{platform}/bug-description`
   > Examples: `feature/v1_6ull/add-audio-collect`, `fix/v1_6ull/rtsp-idle-mem-leak`
3. Implement modifications strictly in accordance with project coding specifications
4. Run full functional tests to avoid introducing new defects
5. Create Pull Request targeting the corresponding `release/{platform}` branch (**DO NOT open PR directly to main branch**)
6. Link related issue number inside PR description if applicable, and briefly summarize your modification scope

## Code Style
- Consistently follow existing project C coding specification including indent rule and variable naming convention
- Add detailed comments for complex logic to illustrate design intention and constraints
- Keep every commit atomic: one commit only solves one single problem
- Standard commit message template: `[ModuleName]: concise modification description`
  > Example: `[rtsp_service]: fix memory leak when no client online`

## Communication
All project-related discussions are held publicly on GitHub Issues and Pull Requests. The open discussion mode keeps full transparency and allows all developers to check history and join in discussion.

Feel free to raise your doubts or suggestions by opening new Issues anytime.