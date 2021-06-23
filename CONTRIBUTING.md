# How to contribute

I'm really glad you're reading this, because we need volunteer to help this project to became better and better.

## Submitting changes

Please send a GitHub Pull Request with  a clear list of what you've done and why.

Always write a clear log message for your commits. One-line messages are fine for small changes, but bigger changes should look like this:

$ git commit -m "A brief summary of the commit

> A paragraph describing what changed and its impact."

Take care to not deteriorate code coverage!

## Testing new hardware

We try to have the most compliant EtherCAT master stack as possible, but we only have access to a few EtherCAT slaves to test it throughfully. Hardware world is always full of  edge cases and reporting us which board doesn't work with the KickCAT debug trace plus a network trace is really helpful to investigate what we are doing wrong or what needs to be improved.
