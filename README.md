# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3)
- flecs checked out at `../../flecs` and built once with `bake3 ../../flecs`
- A Rust toolchain

## Build

```sh
bake3 --local-env
```

## Run

```sh
bake3 run apps/boxrun --local-env -- etc/scenes/town.flecs
```

## Test

```sh
bake3 run test/cpu/core --local-env -- -j 8
python3 tools/integration.py visual
```

## Screenshots
<img width="1600" height="973" alt="image" src="https://github.com/user-attachments/assets/f79c8f68-4813-4564-88e7-5a14485cdaf8" />
<img width="800" height="504" alt="Screenshot 2026-11-02 at 1 45 28 PM" src="https://github.com/user-attachments/assets/bb85ef3b-0f08-4391-a2cc-d6f01fe4db93" />
<img width="1600" height="997" alt="image" src="https://github.com/user-attachments/assets/5c39170a-b799-4e6f-beda-3de1e1baca12" />
<img width="1600" height="898" alt="image" src="https://github.com/user-attachments/assets/d98127e7-e923-4d96-a2f4-b92f8a3a8113" />
<img width="1600" height="1010" alt="image" src="https://github.com/user-attachments/assets/465b29c5-e6ba-406c-b95b-9787aa8d485b" />
<img width="3966" height="2502" alt="image" src="https://github.com/user-attachments/assets/9d370b09-d7be-4b59-b65e-0c6c4b898a2b" />
<img width="1980" height="1241" alt="Screenshot 2026-10-20 at 12 24 00 AM" src="https://github.com/user-attachments/assets/7384e4e7-15fd-430f-bd1d-d8a79324d61d" />
<img width="1600" height="959" alt="image" src="https://github.com/user-attachments/assets/2b8abc1f-643f-4ac7-a116-97c59f167c65" />
