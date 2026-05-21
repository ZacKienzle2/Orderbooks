# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html).
Entries are generated from [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/)
by [git-cliff](https://git-cliff.org).

## [Unreleased]

### Added

- **bench:** Add engine throughput baseline ([ffb37d0](https://github.com/ZacKienzle2/Orderbooks/commit/ffb37d018b907e30c201125501e4d468f4207377))

- **infra:** Thread pinning and name helpers for shard worker threads ([fc93b1c](https://github.com/ZacKienzle2/Orderbooks/commit/fc93b1c7e42dcd2e9f692c3d533e7f87bbfb2264))

- **viz:** Animated top-of-book replay from the event log ([8dc3e65](https://github.com/ZacKienzle2/Orderbooks/commit/8dc3e6579970c06acbfea8a49123dd546071171a))

- **infra:** Multi-symbol shard router over per-symbol engines ([2b37f62](https://github.com/ZacKienzle2/Orderbooks/commit/2b37f626af9dd9042f4317e9137ba08bd2111ae2))

- **engine:** Snapshot and warm-start with a portable wire format ([b57f522](https://github.com/ZacKienzle2/Orderbooks/commit/b57f5221d8cc4556b5f2e698f401e6257e89d850))

- **viz:** Python harness for event-log plots and a streamlit dashboard ([93bf7e6](https://github.com/ZacKienzle2/Orderbooks/commit/93bf7e6167cba913477e0a3caceb8de94a8471f1))

- **app:** Json-lines event recorder and replay binary ([b1945d7](https://github.com/ZacKienzle2/Orderbooks/commit/b1945d746a609d2ad30666ef315cf777611799fd))

- **engine:** Self-cross policy enforcement on the fill path ([6a327fd](https://github.com/ZacKienzle2/Orderbooks/commit/6a327fd4217dee8e17afd91e9b45582d408a50b4))

- **domain:** Account_id field on order and submit_msg ([74a67de](https://github.com/ZacKienzle2/Orderbooks/commit/74a67debfd8a1f2f29cb71ad0efcbbae5e3e5098))

- **engine:** Price-time-priority matching engine with GTC, IOC, FOK ([fb1afab](https://github.com/ZacKienzle2/Orderbooks/commit/fb1afab80eef924bc484cacd046f74fe3671d879))

- **infra:** Bitmap successor / predecessor queries for FOK precheck ([b8c0335](https://github.com/ZacKienzle2/Orderbooks/commit/b8c0335c0cba0070ef64378f28f1b1c4ce8bb57d))

- **book:** Price level and dense-ladder book sides ([8a87700](https://github.com/ZacKienzle2/Orderbooks/commit/8a877002b7eb0b53f86d16e7e766cd2f80e14f47))

- **domain:** Cache-aligned order POD and order-id index ([93a8e22](https://github.com/ZacKienzle2/Orderbooks/commit/93a8e2272244449c11ece7323b6421577111517c))

- **domain:** Commands, events, concepts, and engine config ([7bd5bc6](https://github.com/ZacKienzle2/Orderbooks/commit/7bd5bc61ba5df6a742b96fd8dd8a474b76acf0fa))

- **infra:** Bounded SPSC ring for boundary messaging ([593428e](https://github.com/ZacKienzle2/Orderbooks/commit/593428eb2889780484a317acfa481dc9c817b2cd))

- **infra:** Slab arena with intrusive freelist ([0a94563](https://github.com/ZacKienzle2/Orderbooks/commit/0a94563ff7936626b333face5a9d03ad6e723f78))

- **infra:** Hierarchical bitmap for best-price queries ([915523c](https://github.com/ZacKienzle2/Orderbooks/commit/915523ce8dec8c4b6696792eec6489e6fab138d3))


### Build

- **cmake:** Add CMake, vcpkg, clang tooling, and C++ project skeleton ([cc81fd9](https://github.com/ZacKienzle2/Orderbooks/commit/cc81fd93b6da57d73a4fb5b43ec10daece9ec71c))


### CI

- **bench:** Use median across measurements and validate threshold env var ([4b51e57](https://github.com/ZacKienzle2/Orderbooks/commit/4b51e578cf83fa39ac75ced3b03e120bb1bd000e))

- **bench:** Fail microbench job on CPU-time regression past threshold ([b8e2e6c](https://github.com/ZacKienzle2/Orderbooks/commit/b8e2e6c16bc3613170edc85ea2bfa29d3ac2335c))

- **codeql:** Scan c-cpp and python in addition to actions ([6b702e5](https://github.com/ZacKienzle2/Orderbooks/commit/6b702e57bc3b332001c0d2f74da2c2879e3cea62))

- **actions:** Add github actions workflows and repository metadata ([f27d33a](https://github.com/ZacKienzle2/Orderbooks/commit/f27d33aa9b972fd4ec03591af51b734aa7a0b895))


### Changed

- **shard-router:** Wrap debug owner in std::atomic for torn-read safety ([1022e37](https://github.com/ZacKienzle2/Orderbooks/commit/1022e37a13b6ac0601df672a4efaae8d66526242))

- **book:** Static_assert that book_side stays cache-line aligned ([9411e41](https://github.com/ZacKienzle2/Orderbooks/commit/9411e41dc479b4181980403ffd36d2440287e819))

- **viz:** Drop unnecessary DataFrame .copy() calls in read-only renderers ([ea34302](https://github.com/ZacKienzle2/Orderbooks/commit/ea343023158f5a98b0f2b34fd05906442ee82118))

- **viz:** Re-sort imports in test_viz_smoke.py for ruff isort ([7c1b6f2](https://github.com/ZacKienzle2/Orderbooks/commit/7c1b6f2a5912b0f92bc08ba21db4d204af46a5b6))

- **format:** Apply clang-format to bench casts and affinity helpers ([e71e570](https://github.com/ZacKienzle2/Orderbooks/commit/e71e570873010f59ec9cef77a088525a2cdedd08))

- **viz:** Drop unused pandas import from replay_anim ([bdd6dad](https://github.com/ZacKienzle2/Orderbooks/commit/bdd6dadefb646b371406ca696b39483dfab942cc))

- **cmake:** Apply cmake-format across the tree ([186bc54](https://github.com/ZacKienzle2/Orderbooks/commit/186bc54499e68f44ba65f4be063c9482659a66af))

- **viz:** Satisfy ruff lint and format on the python harness ([1782340](https://github.com/ZacKienzle2/Orderbooks/commit/17823401c30e09e679df83ecb1ff8d91e3bb8c16))

- **format:** Apply clang-format across the C++ tree ([35e8898](https://github.com/ZacKienzle2/Orderbooks/commit/35e8898ccd3ff4cb8b826fd00dde35b95b12dfe3))


### Documentation

- **cmake:** Flag -ffp-contract=fast and -fno-trapping-math rounding implications ([e475a07](https://github.com/ZacKienzle2/Orderbooks/commit/e475a078f8483aee9936d5c3caba246135b0707d))

- **spsc:** Pin observer best-effort semantics and cache ownership ([e7dac63](https://github.com/ZacKienzle2/Orderbooks/commit/e7dac636cb5f3ad211b527033843abad7aa750fe))

- **adr:** Record snapshot wire format and shard router decisions ([900103e](https://github.com/ZacKienzle2/Orderbooks/commit/900103ed87d02d6709412d2e8fbecc31101485d3))

- **adr:** List ADR-0013 in the cluster index ([112a810](https://github.com/ZacKienzle2/Orderbooks/commit/112a8101145788c999daf7d1c2d17fa0fd3439ca))

- **adr:** Record account-aware self-cross semantics ([af4aca1](https://github.com/ZacKienzle2/Orderbooks/commit/af4aca1ee225afa12ddc7f33c4b70289513a7e51))

- **adr:** Record initial architecture decisions and dev guides ([dfdfe7b](https://github.com/ZacKienzle2/Orderbooks/commit/dfdfe7b09b0c8623e8acf61b050ed31a0991cca5))


### Fixed

- **tests:** Drop useless cast in id_index shift-back chain test ([ddc468e](https://github.com/ZacKienzle2/Orderbooks/commit/ddc468ed50ad46809afa4e45397d53801baf12d9))

- **engine:** Assert suppress_top_depth nesting cap before increment ([79a0620](https://github.com/ZacKienzle2/Orderbooks/commit/79a06200160a81646035ce618c755b423eebbb39))

- **json-recorder:** Assert stream has no exceptions enabled at construction ([a3f9099](https://github.com/ZacKienzle2/Orderbooks/commit/a3f909973fe59d21c491f0d36e20e9acfac8fb05))

- **id-index:** Guard reserved sentinel id and load-factor invariant ([3d956dc](https://github.com/ZacKienzle2/Orderbooks/commit/3d956dc4531887e3970edfb2da9c7984d105cf90))

- **bitmap:** Drop useless static_cast on negation of uint64_t hint ([a5d37c8](https://github.com/ZacKienzle2/Orderbooks/commit/a5d37c8866140d4f89f8d96c7cd3c40d2ca51dd4))

- **build:** Tame GCC array-bounds and useless-cast across recorder and engine bench ([ba96193](https://github.com/ZacKienzle2/Orderbooks/commit/ba961937975f40f2cf857f15fe6c897178f0aa16))

- **json-recorder:** Satisfy GCC -Werror=useless-cast and -Werror=array-bounds ([fa10773](https://github.com/ZacKienzle2/Orderbooks/commit/fa10773dff3f90e07133b91c71846a54b9ef59b9))

- **bench:** Respect modify_msg declaration order in designated initializer ([67953eb](https://github.com/ZacKienzle2/Orderbooks/commit/67953eb92c3fd2418f311d296270f1d949531439))

- **shard_router:** Assert single-owner thread on every dispatch (debug) ([b43ca4b](https://github.com/ZacKienzle2/Orderbooks/commit/b43ca4bee6e04636922f8bd9af5466007dd509b2))

- **engine:** Coalesce duplicate top_msg emitted by price-change modify ([3dd3ad3](https://github.com/ZacKienzle2/Orderbooks/commit/3dd3ad350e6b4b67d15227d52402d81805ff09bb))

- **engine:** Static_assert publisher::publish noexcept contract at instantiation ([cd30a7b](https://github.com/ZacKienzle2/Orderbooks/commit/cd30a7b5696ad9b6466ea75e62402bb1e5921af2))

- **arena:** Enforce nothrow-destructible contract on slot type ([1835046](https://github.com/ZacKienzle2/Orderbooks/commit/18350463001e799de01f4b961b4307894cdf142f))

- **snapshot:** Enforce little-endian host and drop false noexcept on growable sink ([5d361eb](https://github.com/ZacKienzle2/Orderbooks/commit/5d361eb49fac327843ae4bba197174103923cc84))

- **tests:** Drop libstdc++-incompatible trivially-destructible assert on order ([88bd076](https://github.com/ZacKienzle2/Orderbooks/commit/88bd076b7274592854d581ef1b7838703c7a0c19))

- **build:** Satisfy GCC -Werror=useless-cast and -Werror=missing-field-initializers ([5a920f6](https://github.com/ZacKienzle2/Orderbooks/commit/5a920f612d904b6d726894ad38b2f2edb3463890))

- **bench:** Drop useless static_cast on state.iterations() ([d5006c8](https://github.com/ZacKienzle2/Orderbooks/commit/d5006c8acc894b4a3dc463ad0b43c90078c50d4c))

- **build:** Drop is_trivially_destructible static_assert and useless size cast ([25139ae](https://github.com/ZacKienzle2/Orderbooks/commit/25139ae3cdc98dbedf510a107c5069adbd4a090c))

- **infra:** Make slab_arena tolerate types whose destructors libstdc++ does not optimise away ([8a337b9](https://github.com/ZacKienzle2/Orderbooks/commit/8a337b9b94c2b2e91560492346ba555e55f60cc8))

- **docs:** Drop bullet-continuation that markdownlint reads as plus-list ([a8aede7](https://github.com/ZacKienzle2/Orderbooks/commit/a8aede7a902db960771f21f147b17629fc6c2a5a))

- **security:** Run pip-audit via pipx instead of the gh-action wrapper ([e0fdcea](https://github.com/ZacKienzle2/Orderbooks/commit/e0fdcea1d773cf8dabec58fcefc6d49b81a41cb2))

- **ci:** Make link-check advisory and repoint pip-audit at v1 tag ([34687ab](https://github.com/ZacKienzle2/Orderbooks/commit/34687ab50f8ecc98f58b6559249c815aed0a5096))

- **ci:** Clear lint stack residue across typos, shellcheck, and yamllint ([219f709](https://github.com/ZacKienzle2/Orderbooks/commit/219f709d11687b071473435e5f4ab5edf6cef9ec))

- **infra:** Include <cstring> for std::memcpy in slab_arena ([6ed2ac3](https://github.com/ZacKienzle2/Orderbooks/commit/6ed2ac3f6d3579e3025e68dc685ad3f4ea4245e7))

- **ci:** Rewrite .zizmor.yml against the 1.5.x rules schema ([84ed900](https://github.com/ZacKienzle2/Orderbooks/commit/84ed900bd7c97cccff1fa292d83e259f5cf74feb))

- **ci:** Correct git-cliff invocation, harness pytest skip, and actionlint shellcheck flags ([36dced4](https://github.com/ZacKienzle2/Orderbooks/commit/36dced4a791ab3d477cfea1c830d3b8ad28ed592))

- **cmake:** Omit -pie linker flag on Apple toolchains ([ce3c9ee](https://github.com/ZacKienzle2/Orderbooks/commit/ce3c9ee19420c7e6476e907ee293b4d87056be79))

- **ci:** Repoint orhun/git-cliff-action at its v4 tag ([5b0e554](https://github.com/ZacKienzle2/Orderbooks/commit/5b0e5549d7e36c4336857ae7aefee0ab10b0f892))

- **docs:** Wrap bare URLs, fix stale ADR cross-link, drop placeholder file refs ([3afb8a4](https://github.com/ZacKienzle2/Orderbooks/commit/3afb8a4d90fa9261866efa10e47420ba89abdf9f))

- **scripts:** Replace ls | grep with native glob in adr-new ([90d97c7](https://github.com/ZacKienzle2/Orderbooks/commit/90d97c7c7fe2b75f60838ef79c009f391b189acf))

- **typos:** Allowlist domain-specific abbreviations on the matching path ([c3d20ee](https://github.com/ZacKienzle2/Orderbooks/commit/c3d20eed0a4b1fcdf977e020439c046108d13ac0))

- **build:** Declare LOB_BUILD_TESTS before Dependencies includes find_package ([e736f5f](https://github.com/ZacKienzle2/Orderbooks/commit/e736f5f18f86aafce3640e70540a64954c4c8392))

- **bench:** Remove inline pause / resume and per-invocation static state ([aa75d8d](https://github.com/ZacKienzle2/Orderbooks/commit/aa75d8da57fcefa2cf8cb3e038d0587733eb95a4))

- **build:** Satisfy strict warnings on Apple Clang and pin vcpkg baseline ([f11772f](https://github.com/ZacKienzle2/Orderbooks/commit/f11772f87ad7b75c4155bbac560b22c34cb693c1))

- **build:** Correct vcpkg baselines, macOS triplet, hardening probes, and CI tooling ([2f04209](https://github.com/ZacKienzle2/Orderbooks/commit/2f04209a62835e2d1f9c69ac9db6cfb1f7b41e05))

- **pre-commit:** Use system binaries for tools that ship native binaries ([1bc544c](https://github.com/ZacKienzle2/Orderbooks/commit/1bc544c03cd6b551dd0d88893506c63958c4100f))


### Maintenance

- **viz:** Make orderbooks_viz an installable package ([3206433](https://github.com/ZacKienzle2/Orderbooks/commit/32064338d425bc74a41a812d36663d43d6707e53))

- **pre-commit:** Exclude vendored and ephemeral trees from gitleaks ([af193d2](https://github.com/ZacKienzle2/Orderbooks/commit/af193d2dc0e1e84c016c688ff63ecf3843fcad32))

- **repo:** Scaffold project metadata, governance, and lint configs ([746a9a8](https://github.com/ZacKienzle2/Orderbooks/commit/746a9a8b762551d4c888f03015810ae00595d7ec))


### Performance

- **engine:** Upgrade cancel/modify order prefetch to a write hint ([54d7bb0](https://github.com/ZacKienzle2/Orderbooks/commit/54d7bb0f6c5424966a4042ed876449ee97a23519))

- **viz:** Cache EventLog by reference via st.cache_resource ([f6b52c3](https://github.com/ZacKienzle2/Orderbooks/commit/f6b52c3695a12a161f9fed08ef669b4add79c0c0))

- **viz:** Fill event_log columns via np.fromiter and fail loud on missing keys ([fb45fa0](https://github.com/ZacKienzle2/Orderbooks/commit/fb45fa03ecf650b513e2a2c13991e112d00d9f8e))

- **id-index:** Defer key/value first-touch to the consuming thread ([b500bce](https://github.com/ZacKienzle2/Orderbooks/commit/b500bce36d993933920189fa3206e770c4f41293))

- **id-index:** Replace unordered_dense with SoA open-addressed table ([2ca6bdd](https://github.com/ZacKienzle2/Orderbooks/commit/2ca6bdd6a04ecd2e4c4c47e99aefd924a8d6ef58))

- **arena:** Defer freelist init to first allocate for NUMA first-touch ([7f80457](https://github.com/ZacKienzle2/Orderbooks/commit/7f80457a2473d7537ffcffa79402f12335603609))

- **bitmap:** Branchless cascading clear across all four tiers ([746652e](https://github.com/ZacKienzle2/Orderbooks/commit/746652e23f24e74077d6502aff4addf3e5f47b7a))

- **viz:** Cache event_log read in the Streamlit dashboard ([23a057e](https://github.com/ZacKienzle2/Orderbooks/commit/23a057ed91916c8c042b8e6e2aa39d704b055b2a))

- **viz:** O(log n) top-of-book lookup in depth.at_seq via searchsorted ([a361d54](https://github.com/ZacKienzle2/Orderbooks/commit/a361d54072923a4c1b4b6c57d0f8f0ad490f3bf1))

- **viz:** Orjson parse + columnar DataFrame build in event_log ([31e0f52](https://github.com/ZacKienzle2/Orderbooks/commit/31e0f5266aebcb82dad19721b8d295dac00643c1))

- **engine,book:** Align bid/ask/arena to cache lines and tag hot/cold paths ([ff04695](https://github.com/ZacKienzle2/Orderbooks/commit/ff046950c0a2644120e1e7e341347f3568b1462f))

- **cmake:** Probe -falign-functions/loops and fast-math flags in Release ([3386563](https://github.com/ZacKienzle2/Orderbooks/commit/3386563c0503df7ab01215c852489319c5456378))

- **spsc:** Cache the remote cursor locally to skip cross-core loads on the common path ([ed4badc](https://github.com/ZacKienzle2/Orderbooks/commit/ed4badcaf2bfa3c278dc6b4968dde09d114ee10e))

- **engine:** Drive cold-path scans from the bitmap ([ba2e2ac](https://github.com/ZacKienzle2/Orderbooks/commit/ba2e2ac7f31004d0f4786de50feb488cb6b4beae))

- **engine:** Prefetch the order line after id_index lookup on cancel and modify ([6b77fd0](https://github.com/ZacKienzle2/Orderbooks/commit/6b77fd0b7836839ccd1e3a7f2b25cc4f082e684f))

- **json-recorder:** Replace ostream operator<< chain with std::to_chars + single write ([3416e64](https://github.com/ZacKienzle2/Orderbooks/commit/3416e64652e6bcda1bcec0f63c56d8c2f7d1e423))

- **infra:** Hierarchical descent for next_set_at_or_after and its mirror ([d684b7f](https://github.com/ZacKienzle2/Orderbooks/commit/d684b7fe251ffb6b6c6e69c4da6c10f9de43e46f))

- **engine:** Short-circuit publish_top via a dirty flag ([6beab8d](https://github.com/ZacKienzle2/Orderbooks/commit/6beab8d0df04aa35a389b741bfd798db24ff58c5))

- **engine:** Isolate hot mutable state on its own cache line ([dac55b0](https://github.com/ZacKienzle2/Orderbooks/commit/dac55b0331ca96bcebff47b8fb28c719272e8153))

- **infra:** Drop constant-time size tracking on order_fifo ([9807c55](https://github.com/ZacKienzle2/Orderbooks/commit/9807c55729c43d1662a0b002a9491de2ad452ea6))


### Tests

- **snapshot:** Cover the sink-throw path through engine::snapshot ([b4dfb60](https://github.com/ZacKienzle2/Orderbooks/commit/b4dfb60645469d67f23ee94cd4091ce85c44d1ab))

- **engine:** Quantity conservation and aggregate / bitmap consistency ([f9973a2](https://github.com/ZacKienzle2/Orderbooks/commit/f9973a2f3fb5b935de973d008a6086ffc959c4bc))

- **engine:** Differential property tests against a std-map reference ([234024a](https://github.com/ZacKienzle2/Orderbooks/commit/234024a5ed391e11daad880f1d1ab07f5a86dd1b))


[Unreleased]: https://github.com/ZacKienzle2/Orderbooks/compare/...HEAD

