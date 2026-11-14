# WeatherRouting / xWeatherRouting 1.36.3

* Automatic adaptive settings are on for new passages, with the user's explicit
  choice remembered. Existing saved/imported routes remain manual until enabled.
* The policy uses boat performance, route geometry and cheap resident weather
  samples to choose supported numerical inputs. Current vector changes favour
  short steps; engines retain their own endpoint/coastal adaptation.
* Switching off restores the original manual inputs exactly. Safety limits and
  user memory ceilings remain unchanged.
* Auto returns the first accepted validated route, including Quick2. All compares
  every selected engine and admitted alternatives.
* Separate Quick2 local-step and Standard recovery variants preserve the original
  solver sources. Work admission considers route complexity and measured memory
  headroom; shared caches retain their existing guards.
* Includes the 1.36.2 provider-backoff/cache fixes.

See [policy, qualification and limits](automatic-adaptive-settings.md).
