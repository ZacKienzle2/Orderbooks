# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).
Entries are generated from [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/)
by [git-cliff](https://git-cliff.org).

## [Unreleased]

### Added

- **profile:** Add synthetic-flow profiler and plugin runner (#50) ([ba219c6](https://github.com/ZacKienzle2/Orderbooks/commit/ba219c607f3933dd80b03027127e36247f0acf75))

- **gateway:** Binary order-entry gateway over TCP (#46) ([ac62d1f](https://github.com/ZacKienzle2/Orderbooks/commit/ac62d1f87ee863ba4b092e1616057c3a44730fbf))

- **loadgen:** End-to-end load harness for throughput and latency (#45) ([69b75e6](https://github.com/ZacKienzle2/Orderbooks/commit/69b75e60823f8f7b9a726f323893444c09b42adc))

- **bench:** Profile engine submit latency with the HDR histogram (#39) ([7ed70d4](https://github.com/ZacKienzle2/Orderbooks/commit/7ed70d47c0ad3670dec477f08eb6f1ee0142a3fd))

- **metrics:** Add a from-scratch HDR latency histogram (#38) ([1af809c](https://github.com/ZacKienzle2/Orderbooks/commit/1af809cfe8bd67e42ca93dfb36c6bf0ea1a8a695))

- **infra:** Publisher-concept seam for the merged egress stream (#35) ([34370b7](https://github.com/ZacKienzle2/Orderbooks/commit/34370b72c57d084c1188a174ac9bac29ce65274e))

- **infra:** Threaded shard runtime, per-shard egress, merging consumer (#33) ([0859a90](https://github.com/ZacKienzle2/Orderbooks/commit/0859a90312c75a9728358691fbcbb7cb58e4ec5e))

- **fix:** Zero-copy FIX 4.4 order-entry parser (#30) ([95c9222](https://github.com/ZacKienzle2/Orderbooks/commit/95c9222487d964a21d25e11c86c7022f062ff017))

- **bench:** Add engine throughput baseline ([ba8bce1](https://github.com/ZacKienzle2/Orderbooks/commit/ba8bce1187f9f3809305dd6a4928c21b1aad8376))

- **infra:** Thread pinning and name helpers for shard worker threads ([1ff83b2](https://github.com/ZacKienzle2/Orderbooks/commit/1ff83b2a253bb56865ecc3ccc74d791392376569))

- **viz:** Animated top-of-book replay from the event log ([291177c](https://github.com/ZacKienzle2/Orderbooks/commit/291177c3e23bbdaf40ad930ee962da61a2544ec6))

- **infra:** Multi-symbol shard router over per-symbol engines ([857a925](https://github.com/ZacKienzle2/Orderbooks/commit/857a9254fd707685bbafb7cf5443d560b6ec08e3))

- **engine:** Snapshot and warm-start with a portable wire format ([7033284](https://github.com/ZacKienzle2/Orderbooks/commit/7033284c1f6fa52e3260a70cdb64f619397625b0))

- **viz:** Python harness for event-log plots and a streamlit dashboard ([5ca597a](https://github.com/ZacKienzle2/Orderbooks/commit/5ca597a48ba82ef556a652ce41e518a55de044c0))

- **app:** Json-lines event recorder and replay binary ([6179c19](https://github.com/ZacKienzle2/Orderbooks/commit/6179c19870ec88ab24ee8bedd3d2edc7678bbbc7))

- **engine:** Self-cross policy enforcement on the fill path ([8b36cb3](https://github.com/ZacKienzle2/Orderbooks/commit/8b36cb3a698b2cebc5a8044d105ee0a4efe6f9bb))

- **domain:** Account_id field on order and submit_msg ([1f8b77f](https://github.com/ZacKienzle2/Orderbooks/commit/1f8b77f5296fb0725fdcf97fee08ce9fb14eff6c))

- **engine:** Price-time-priority matching engine with GTC, IOC, FOK ([7f1060e](https://github.com/ZacKienzle2/Orderbooks/commit/7f1060e40f0d99a352e0dd5e0d7ff2f2f93061c5))

- **infra:** Bitmap successor / predecessor queries for FOK precheck ([d2be3af](https://github.com/ZacKienzle2/Orderbooks/commit/d2be3af2c493df8d7e5e7d9f0b698f7766c7613d))

- **book:** Price level and dense-ladder book sides ([2ff696f](https://github.com/ZacKienzle2/Orderbooks/commit/2ff696fba89de0c091e5d9bf7c14e1ab056f776b))

- **domain:** Cache-aligned order POD and order-id index ([8364c00](https://github.com/ZacKienzle2/Orderbooks/commit/8364c006b7d1e490bf9f214b680cf80250232dbf))

- **domain:** Commands, events, concepts, and engine config ([eb64192](https://github.com/ZacKienzle2/Orderbooks/commit/eb64192b4bd7b43c01da39465f0a88e7116ba66f))

- **infra:** Bounded SPSC ring for boundary messaging ([0414d10](https://github.com/ZacKienzle2/Orderbooks/commit/0414d104078761ddfb3cd18a59a17d17deb7efdc))

- **infra:** Slab arena with intrusive freelist ([a043c7d](https://github.com/ZacKienzle2/Orderbooks/commit/a043c7d4e1f08791fd3fd7be2e7cb759e4ec13d5))

- **infra:** Hierarchical bitmap for best-price queries ([de703a5](https://github.com/ZacKienzle2/Orderbooks/commit/de703a56241e407995eabd45d8fa02a59564b2d4))


### Build

- **cmake:** Add CMake, vcpkg, clang tooling, and C++ project skeleton ([0816683](https://github.com/ZacKienzle2/Orderbooks/commit/081668313ff6b59a8dcdef6f9286e95c576dadf5))


### CI

- **cmake:** Trigger the build matrix on apps changes (#56) ([9aeb314](https://github.com/ZacKienzle2/Orderbooks/commit/9aeb31457efff35b5a95621b98df9b77bb96e199))

- **bench:** Gate engine latency against an absolute ceiling (#41) ([62b8e93](https://github.com/ZacKienzle2/Orderbooks/commit/62b8e93302e2a75dcba52d8efeeff4465d027ae7))

- **clang-tidy:** Enforce lint by fixing module-scan and aligning policy (#34) ([40703e3](https://github.com/ZacKienzle2/Orderbooks/commit/40703e3aa23dc46e47f920a4f1c62bd449694f26))

- **deps:** Bump actions/stale from 9.1.0 to 10.2.0 (#7) ([55d71b2](https://github.com/ZacKienzle2/Orderbooks/commit/55d71b25e8b3c61b149941c742f1bf31423fde98))

- **deps:** Bump actions/checkout from 4.3.1 to 6.0.2 (#5) ([461e51c](https://github.com/ZacKienzle2/Orderbooks/commit/461e51c22775af3aebb0fcc18e410d383cad0de7))

- **deps:** Bump crate-ci/typos in the actions-minor-and-patch group (#1) ([36c1d00](https://github.com/ZacKienzle2/Orderbooks/commit/36c1d00e2a765ba483c2074cb619cf6c2ec25627))

- **deps:** Bump actions/upload-artifact from 4.4.3 to 7.0.1 (#2) ([8569e88](https://github.com/ZacKienzle2/Orderbooks/commit/8569e881cba01bb7278748eac9aeee71504e064d))

- **deps:** Bump dessant/lock-threads from 5.0.1 to 6.0.0 (#3) ([007d71f](https://github.com/ZacKienzle2/Orderbooks/commit/007d71f66e85152d6d66941cf981b5bd5329baec))

- **deps:** Bump google/osv-scanner-action/.github/workflows/osv-scanner-reusable.yml (#6) ([ee516dc](https://github.com/ZacKienzle2/Orderbooks/commit/ee516dc8449ebee4eb6be0f0b99003175f213242))

- **deps:** Bump softprops/action-gh-release from 2.6.2 to 3.0.0 (#8) ([3d377f0](https://github.com/ZacKienzle2/Orderbooks/commit/3d377f0e24ed3329428a4493e7ed67d4a63ccfcc))

- **deps:** Bump github/codeql-action from 3.35.5 to 4.35.5 (#9) ([3474ce9](https://github.com/ZacKienzle2/Orderbooks/commit/3474ce9ee74dfab1910afd41af6f548f6410a1a2))

- **deps:** Bump actions/first-interaction from 1 to 3 (#4) ([bbb26fc](https://github.com/ZacKienzle2/Orderbooks/commit/bbb26fca9a629aa10526d09f74de0c2bb663d0b6))

- **deps:** Bump seanmiddleditch/gha-setup-ninja from 4 to 6 (#10) ([d2acb70](https://github.com/ZacKienzle2/Orderbooks/commit/d2acb700a81364cd1057f3531030318c78101dbb))

- **bench:** Use median across measurements and validate threshold env var ([f1ce595](https://github.com/ZacKienzle2/Orderbooks/commit/f1ce595d03668326c4082afe9b0a9340f478f912))

- **bench:** Fail microbench job on CPU-time regression past threshold ([dd16e16](https://github.com/ZacKienzle2/Orderbooks/commit/dd16e16a6e41478ee4f8c8bfe07232692f4143ab))

- **codeql:** Scan c-cpp and python in addition to actions ([43820be](https://github.com/ZacKienzle2/Orderbooks/commit/43820be331f42e6a356dc5db4a9b60330de7570f))

- **actions:** Add github actions workflows and repository metadata ([32a1eb5](https://github.com/ZacKienzle2/Orderbooks/commit/32a1eb5dc4d349d449e702528bc6cd636b1e46ad))


### Changed

- **shard-router:** Wrap debug owner in std::atomic for torn-read safety ([d7f9dce](https://github.com/ZacKienzle2/Orderbooks/commit/d7f9dce91afb53cfb36f0fcf55df0b0aa2d66361))

- **book:** Static_assert that book_side stays cache-line aligned ([07b1447](https://github.com/ZacKienzle2/Orderbooks/commit/07b1447fa1481dc73125e304ca7e7634a168818c))

- **viz:** Drop unnecessary DataFrame .copy() calls in read-only renderers ([8af0599](https://github.com/ZacKienzle2/Orderbooks/commit/8af059918822562117ca72793772dee300b72b62))

- **viz:** Re-sort imports in test_viz_smoke.py for ruff isort ([4daa960](https://github.com/ZacKienzle2/Orderbooks/commit/4daa96014ad844fb75433314c52a9eddfe317088))

- **format:** Apply clang-format to bench casts and affinity helpers ([14003cb](https://github.com/ZacKienzle2/Orderbooks/commit/14003cbb244ff4450e73e21717b3ca8f420feb4c))

- **viz:** Drop unused pandas import from replay_anim ([ff77109](https://github.com/ZacKienzle2/Orderbooks/commit/ff7710901bcf3280bb95048fe0f5bb53fed51043))

- **cmake:** Apply cmake-format across the tree ([600fa29](https://github.com/ZacKienzle2/Orderbooks/commit/600fa29b23b15c77a596dd5a54d74fca983dd1ae))

- **viz:** Satisfy ruff lint and format on the python harness ([c0b4e5b](https://github.com/ZacKienzle2/Orderbooks/commit/c0b4e5bf1c7c87fb8b57cda40627d37ec64ebab3))

- **format:** Apply clang-format across the C++ tree ([9dbe6bb](https://github.com/ZacKienzle2/Orderbooks/commit/9dbe6bb8eec8d79dfdc43c25e2a025d2b68f224b))


### Documentation

- **adr:** Record AoS id-index layout and engine cache floor (#52) ([848b731](https://github.com/ZacKienzle2/Orderbooks/commit/848b731cd924539c672dd10afe4eed32d4dea52b))

- Drop the trading-simulation phrasing from the about line (#37) ([0841565](https://github.com/ZacKienzle2/Orderbooks/commit/08415658fc04995318f0add58cc2c426783181e2))

- **cmake:** Flag -ffp-contract=fast and -fno-trapping-math rounding implications ([2f0b7a8](https://github.com/ZacKienzle2/Orderbooks/commit/2f0b7a86a64406705ca9cf4f74a3b5c4a621825f))

- **spsc:** Pin observer best-effort semantics and cache ownership ([2bb7188](https://github.com/ZacKienzle2/Orderbooks/commit/2bb7188ff2eeb0a9eaf6ab01606926e2ba8e2ae6))

- **adr:** Record snapshot wire format and shard router decisions ([066bc82](https://github.com/ZacKienzle2/Orderbooks/commit/066bc8233a3aab95337cd906cd37dd48d67e529e))

- **adr:** List ADR-0013 in the cluster index ([9092f18](https://github.com/ZacKienzle2/Orderbooks/commit/9092f183485a8eff7dc971b8998307c9c0f2b134))

- **adr:** Record account-aware self-cross semantics ([0425d49](https://github.com/ZacKienzle2/Orderbooks/commit/0425d493b19f0fba4d3fee139d56e9269ab8106d))

- **adr:** Record initial architecture decisions and dev guides ([4beafab](https://github.com/ZacKienzle2/Orderbooks/commit/4beafab72b1f0fda2ccbc5e6df8d213e10408afe))


### Fixed

- **gateway:** Collapse EAGAIN or EWOULDBLOCK test under -Wlogical-op (#48) ([c6f02f7](https://github.com/ZacKienzle2/Orderbooks/commit/c6f02f72e2cb57ddde34c26d8fed57e91cd55edc))

- **tests:** Drop useless cast in id_index shift-back chain test ([a3f2cbe](https://github.com/ZacKienzle2/Orderbooks/commit/a3f2cbea679bad02defa9e0c70c8af932e4a6109))

- **engine:** Assert suppress_top_depth nesting cap before increment ([551f7b7](https://github.com/ZacKienzle2/Orderbooks/commit/551f7b7fde1bc1b53a9d14c71bc86dc02a49fae5))

- **json-recorder:** Assert stream has no exceptions enabled at construction ([8f35b52](https://github.com/ZacKienzle2/Orderbooks/commit/8f35b5272f3583707a2321316f2c8d0c79a7c9fd))

- **id-index:** Guard reserved sentinel id and load-factor invariant ([fde114a](https://github.com/ZacKienzle2/Orderbooks/commit/fde114a9708d19764863b9b49eb5c29dcaae6793))

- **bitmap:** Drop useless static_cast on negation of uint64_t hint ([997989c](https://github.com/ZacKienzle2/Orderbooks/commit/997989c67566fbb45f95d9e0e89cab3bd7deb32a))

- **build:** Tame GCC array-bounds and useless-cast across recorder and engine bench ([f38b4cf](https://github.com/ZacKienzle2/Orderbooks/commit/f38b4cfc7ff675154781c3dbef5ebea73f90c140))

- **json-recorder:** Satisfy GCC -Werror=useless-cast and -Werror=array-bounds ([ed99045](https://github.com/ZacKienzle2/Orderbooks/commit/ed9904555b1a845807a57b2d3d7d366627ff9f9e))

- **bench:** Respect modify_msg declaration order in designated initializer ([a2e4d8c](https://github.com/ZacKienzle2/Orderbooks/commit/a2e4d8cf56bbabd271a077ca460e5dbdc09a5914))

- **shard_router:** Assert single-owner thread on every dispatch (debug) ([985630a](https://github.com/ZacKienzle2/Orderbooks/commit/985630a648110c088238f09ddb72aeebb9f2def0))

- **engine:** Coalesce duplicate top_msg emitted by price-change modify ([b78b5ab](https://github.com/ZacKienzle2/Orderbooks/commit/b78b5ab64ddf1928abd6c1cac9f63380a792dfc9))

- **engine:** Static_assert publisher::publish noexcept contract at instantiation ([d0bed71](https://github.com/ZacKienzle2/Orderbooks/commit/d0bed71c042c1f6f665b5af47ab2f13d3043c7bd))

- **arena:** Enforce nothrow-destructible contract on slot type ([b6cae09](https://github.com/ZacKienzle2/Orderbooks/commit/b6cae0968f37f7dc4d9a6fb490bd92f3cb2655d8))

- **snapshot:** Enforce little-endian host and drop false noexcept on growable sink ([1d63837](https://github.com/ZacKienzle2/Orderbooks/commit/1d63837f4ac2ecd19a01118bded39f8fbda2d42e))

- **tests:** Drop libstdc++-incompatible trivially-destructible assert on order ([73b4f38](https://github.com/ZacKienzle2/Orderbooks/commit/73b4f38c245ab35f4eea171b03a080cfe95c4244))

- **build:** Satisfy GCC -Werror=useless-cast and -Werror=missing-field-initializers ([9438cb0](https://github.com/ZacKienzle2/Orderbooks/commit/9438cb02a3e133a7369bce1f5125cf198e6e2b3e))

- **bench:** Drop useless static_cast on state.iterations() ([743726e](https://github.com/ZacKienzle2/Orderbooks/commit/743726ebc5810a8f77151b00accbb86953a78de3))

- **build:** Drop is_trivially_destructible static_assert and useless size cast ([4d32771](https://github.com/ZacKienzle2/Orderbooks/commit/4d3277153942096be207483a38e8ca835a7c201a))

- **infra:** Make slab_arena tolerate types whose destructors libstdc++ does not optimise away ([bc173d6](https://github.com/ZacKienzle2/Orderbooks/commit/bc173d60997911af17b9ca130132ede2fef3372d))

- **docs:** Drop bullet-continuation that markdownlint reads as plus-list ([6eb7fea](https://github.com/ZacKienzle2/Orderbooks/commit/6eb7feac4754361bf965a067d757ed2eb703d7c8))

- **security:** Run pip-audit via pipx instead of the gh-action wrapper ([cd5e7df](https://github.com/ZacKienzle2/Orderbooks/commit/cd5e7dfa6ba6a408718be385247bcf1ae5b778fa))

- **ci:** Make link-check advisory and repoint pip-audit at v1 tag ([f052a96](https://github.com/ZacKienzle2/Orderbooks/commit/f052a963ed56491adafd3cc47d3e75bf6c971a96))

- **ci:** Clear lint stack residue across typos, shellcheck, and yamllint ([78c75a3](https://github.com/ZacKienzle2/Orderbooks/commit/78c75a3a2c19e778f5e86468ae5fc405078fdd29))

- **infra:** Include <cstring> for std::memcpy in slab_arena ([547e3d1](https://github.com/ZacKienzle2/Orderbooks/commit/547e3d18f9e5a278aeb4baacdbd2333a7f31e377))

- **ci:** Rewrite .zizmor.yml against the 1.5.x rules schema ([ac60c5c](https://github.com/ZacKienzle2/Orderbooks/commit/ac60c5c4fb9036d92ade35f2b5583821ae2e7dc1))

- **ci:** Correct git-cliff invocation, harness pytest skip, and actionlint shellcheck flags ([3308b6e](https://github.com/ZacKienzle2/Orderbooks/commit/3308b6e2779db8821942a0b4b45490850dab1eb9))

- **cmake:** Omit -pie linker flag on Apple toolchains ([528bfa2](https://github.com/ZacKienzle2/Orderbooks/commit/528bfa224ee9ebe9afbf4e89ec4c086b50d21a38))

- **ci:** Repoint orhun/git-cliff-action at its v4 tag ([5af792c](https://github.com/ZacKienzle2/Orderbooks/commit/5af792c79326e26358c2744ba530b54dac764e4c))

- **docs:** Wrap bare URLs, fix stale ADR cross-link, drop placeholder file refs ([8c5fb13](https://github.com/ZacKienzle2/Orderbooks/commit/8c5fb136eddfaeea78d6a921444fc298cb56d14e))

- **scripts:** Replace ls | grep with native glob in adr-new ([26c4379](https://github.com/ZacKienzle2/Orderbooks/commit/26c43796df9e7b6de4ff4bb1969d4cf97236baa5))

- **typos:** Allowlist domain-specific abbreviations on the matching path ([c1a334b](https://github.com/ZacKienzle2/Orderbooks/commit/c1a334bbf07ee3f8e9f48ef0fe6edf95138abf21))

- **build:** Declare LOB_BUILD_TESTS before Dependencies includes find_package ([d3bc7f4](https://github.com/ZacKienzle2/Orderbooks/commit/d3bc7f477a92edd50640a02ff3f0a4aab26a6153))

- **bench:** Remove inline pause / resume and per-invocation static state ([1c1d483](https://github.com/ZacKienzle2/Orderbooks/commit/1c1d4839e4daf29490386f8346cd3e255f7c8006))

- **build:** Satisfy strict warnings on Apple Clang and pin vcpkg baseline ([5557413](https://github.com/ZacKienzle2/Orderbooks/commit/555741311e511be4e8d10e85adea32c90d850246))

- **build:** Correct vcpkg baselines, macOS triplet, hardening probes, and CI tooling ([652da6c](https://github.com/ZacKienzle2/Orderbooks/commit/652da6cb75cc6349dac95ff738fa5aa2c29a55f9))

- **pre-commit:** Use system binaries for tools that ship native binaries ([c8677c0](https://github.com/ZacKienzle2/Orderbooks/commit/c8677c029f05aef3ce954503511ad8df9abe946b))


### Maintenance

- **changelog:** Regenerate from conventional commits (#13) ([def5813](https://github.com/ZacKienzle2/Orderbooks/commit/def581347b029cea441c10e396e84245e4e47c9b))

- **changelog:** Regenerate from conventional commits (#12) ([241d665](https://github.com/ZacKienzle2/Orderbooks/commit/241d665524721955fb48ecfef90d255e3dfbf44e))

- **lint:** Relax commitlint subject-case and markdownlint MD012 for bot PRs ([458c378](https://github.com/ZacKienzle2/Orderbooks/commit/458c378573fd561781cdc61fc8eec559396c95c4))

- **changelog:** Regenerate from conventional commits (#11) ([8a18c32](https://github.com/ZacKienzle2/Orderbooks/commit/8a18c32dc4d541d1f6d2ff91910893aef65e427d))

- **viz:** Make orderbooks_viz an installable package ([9df81eb](https://github.com/ZacKienzle2/Orderbooks/commit/9df81ebc62f76581f9b207859068cd430e9a9083))

- **pre-commit:** Exclude vendored and ephemeral trees from gitleaks ([584ce8a](https://github.com/ZacKienzle2/Orderbooks/commit/584ce8a006579236ac91fcb09b641e2b92a7b079))

- **repo:** Scaffold project metadata, governance, and lint configs ([351ae55](https://github.com/ZacKienzle2/Orderbooks/commit/351ae550ae59e12f7cd0ada4cce9c76842e2c7fc))


### Performance

- **fix:** Fold tag scan and delimiter search into one pass (#54) ([a5a21c2](https://github.com/ZacKienzle2/Orderbooks/commit/a5a21c27377ee657811024984134081601e058d4))

- **spsc:** Batched zero-copy ring drain on the shard worker (#53) ([99296c2](https://github.com/ZacKienzle2/Orderbooks/commit/99296c2fba0ab913c2e9c51ddebf2aac2b946998))

- **id-index:** Co-locate key and value to cut a cache line per probe (#51) ([1c9a317](https://github.com/ZacKienzle2/Orderbooks/commit/1c9a3177ac5270a12902fff35541d3ead6cf33ee))

- **runtime:** Batch the shard worker's quiescence counter (#47) ([548d340](https://github.com/ZacKienzle2/Orderbooks/commit/548d34067d44fae383f822e3890333ead87b8558))

- **engine:** Skip top-of-book recompute when the top cannot move (#44) ([95d7c7d](https://github.com/ZacKienzle2/Orderbooks/commit/95d7c7d205ce42d9711a9aa20514f064bfdae5e5))

- **engine:** Relink resting price-move modify in place (#43) ([1701a78](https://github.com/ZacKienzle2/Orderbooks/commit/1701a78143503541cc45c93f60682845e375071a))

- **engine:** Revert match-sweep prefetch after A/B regression (#42) ([6bf95ee](https://github.com/ZacKienzle2/Orderbooks/commit/6bf95eecfdaf694aa22cbc9ab9d60af67cedbf09))

- **engine:** Prefetch successor in match sweep, hoist self-cross test (#40) ([b54bb3e](https://github.com/ZacKienzle2/Orderbooks/commit/b54bb3e375543a0c62b0adfa5cfb1dbbbfa7c1de))

- **arena:** Back the slab arena with 2 MiB huge pages (#36) ([93f0600](https://github.com/ZacKienzle2/Orderbooks/commit/93f060069603a6e37fc8db48fbe8a5940ea59ad8))

- **engine:** Upgrade cancel/modify order prefetch to a write hint ([bbbc8e3](https://github.com/ZacKienzle2/Orderbooks/commit/bbbc8e3370d2f3a1a13f8a1d5d36b5502a22c1d7))

- **viz:** Cache EventLog by reference via st.cache_resource ([e57c08d](https://github.com/ZacKienzle2/Orderbooks/commit/e57c08de89197d39134a8d20012cdf10ed2dca86))

- **viz:** Fill event_log columns via np.fromiter and fail loud on missing keys ([62cd213](https://github.com/ZacKienzle2/Orderbooks/commit/62cd213f837de26a0cd6a417514795fbd0c3f696))

- **id-index:** Defer key/value first-touch to the consuming thread ([24b585b](https://github.com/ZacKienzle2/Orderbooks/commit/24b585b5e821f548bccba5336510b9657e677fe0))

- **id-index:** Replace unordered_dense with SoA open-addressed table ([2ce806e](https://github.com/ZacKienzle2/Orderbooks/commit/2ce806e0d601081caabe1fbfad0ef3fd30668f20))

- **arena:** Defer freelist init to first allocate for NUMA first-touch ([f285a0e](https://github.com/ZacKienzle2/Orderbooks/commit/f285a0e3263788e36190a3f39191f5bfe82fd0f4))

- **bitmap:** Branchless cascading clear across all four tiers ([aad0576](https://github.com/ZacKienzle2/Orderbooks/commit/aad0576e79e392fb0a662f9a7f66072271103c2b))

- **viz:** Cache event_log read in the Streamlit dashboard ([d0cdc49](https://github.com/ZacKienzle2/Orderbooks/commit/d0cdc49a7479c8bdb5ba47a254256582898053c4))

- **viz:** O(log n) top-of-book lookup in depth.at_seq via searchsorted ([ee3052e](https://github.com/ZacKienzle2/Orderbooks/commit/ee3052ef78645c412c91c9b913fdbd0c0712f5aa))

- **viz:** Orjson parse + columnar DataFrame build in event_log ([d4501e8](https://github.com/ZacKienzle2/Orderbooks/commit/d4501e87fe4d0e40f8618ae5dbab8b0a727bbf75))

- **engine,book:** Align bid/ask/arena to cache lines and tag hot/cold paths ([24c8bce](https://github.com/ZacKienzle2/Orderbooks/commit/24c8bce572a07637c666c30c31ac370e88b93f17))

- **cmake:** Probe -falign-functions/loops and fast-math flags in Release ([3ff3fa9](https://github.com/ZacKienzle2/Orderbooks/commit/3ff3fa954b60c74562cdfeed3d62e97f97ee57e9))

- **spsc:** Cache the remote cursor locally to skip cross-core loads on the common path ([18a5379](https://github.com/ZacKienzle2/Orderbooks/commit/18a5379b47e03fd37ebc86fc885658d93effb3eb))

- **engine:** Drive cold-path scans from the bitmap ([fb70558](https://github.com/ZacKienzle2/Orderbooks/commit/fb7055872247560394dc2d4d34396ee8e6a6a068))

- **engine:** Prefetch the order line after id_index lookup on cancel and modify ([edc6c7d](https://github.com/ZacKienzle2/Orderbooks/commit/edc6c7d9852915a2fee1570d4d16854f3af89e7c))

- **json-recorder:** Replace ostream operator<< chain with std::to_chars + single write ([9c2eb75](https://github.com/ZacKienzle2/Orderbooks/commit/9c2eb75fbe2a99e619255db3884e4c203ec582fb))

- **infra:** Hierarchical descent for next_set_at_or_after and its mirror ([10e9a4b](https://github.com/ZacKienzle2/Orderbooks/commit/10e9a4bda1ebbb5605a6b2be450a05daa6fff217))

- **engine:** Short-circuit publish_top via a dirty flag ([3cefb9a](https://github.com/ZacKienzle2/Orderbooks/commit/3cefb9ab90486fd0f3bf25ef3a7a47034f88d741))

- **engine:** Isolate hot mutable state on its own cache line ([3ffcba3](https://github.com/ZacKienzle2/Orderbooks/commit/3ffcba33d7357ea3bcd29085fa28bfd26e160be9))

- **infra:** Drop constant-time size tracking on order_fifo ([2a874ab](https://github.com/ZacKienzle2/Orderbooks/commit/2a874ab4eede95dedbf4ad648bf30ee266947b77))


### Tests

- **engine:** Randomized torture stream with invariant audits (#49) ([ab78aa8](https://github.com/ZacKienzle2/Orderbooks/commit/ab78aa8346bd2a4e7345c4e6223d22ca8023817d))

- **snapshot:** Cover the sink-throw path through engine::snapshot ([19a60db](https://github.com/ZacKienzle2/Orderbooks/commit/19a60db8f03cea427e649556cdbde3d6e4ff1d91))

- **engine:** Quantity conservation and aggregate / bitmap consistency ([2f27824](https://github.com/ZacKienzle2/Orderbooks/commit/2f278249ec3328cf611afc937225b6d82eca1f5a))

- **engine:** Differential property tests against a std-map reference ([d784ae3](https://github.com/ZacKienzle2/Orderbooks/commit/d784ae3eabcb5e371d614984ea302640d4b9e1ed))


[Unreleased]: https://github.com/ZacKienzle2/Orderbooks/compare/...HEAD

