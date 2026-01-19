# v1.0.3

- fixed crash when saving images.
- deleted useless code.
- cleaned up everything to prevent issues.

# v1.0.2

- **Dependencies**: Updated dependency configurations and optimized download handling.
- **Moderation**: The moderator menu is now hidden by default; added automatic server verification on profile load.

# v1.0.1 (Geode Guidelines & Optimization)

- **Code Auditing**: Full compliance with Geode Modding Guidelines v4.9.0.
- **Safety**: Fixed blocking I/O on main thread (saving images now runs in detached threads).
- **Cleanup**: Removed deprecated features (Dark Mode, Moderator Mode, Profile Gradient) to focus on core functionality.
- **Stability**: Fixed potential crashes with `dynamic_cast` and Windows path handling.
- **Refactoring**: Cleaned up header files to prevent namespace pollution.

# v1.0.0 (Public Release)

- Level thumbnails system.
- High-quality hybrid capture (Direct/Render) from inside the game.
- Optimized concurrent downloading + local cache.
- Configurable visual effects (gradients/particles/hover).
- Advanced display options (including background styles in the level info screen).
- Mod settings integration (including performance and customization options).


## Coming Soon

- Versions for **Android**, **iOS**, and **macOS**.
