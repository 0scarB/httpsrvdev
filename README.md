A simple HTTP development server in C.

Help message:
```
httpsrvdev [OPTIONS/FLAGS] [SRC1 SRC2 ...]

Serve files and directories via HTTP.
For non-deployment (a.k.a. non-production) software development.

[SRC1 SRC2 ...] .... A list of 0 or more sources. A source may
                     a) be the path to a file or a directory
                     b) be "-", as a placeholder for the standard input.
                           If 0 sources are provided, the CWD (current
                           working directory) will be served.
                           If 1 source is is provided, only that source will
                           be served.
                           If multiple sources are provided, a listing that
                           links to each source will be served.
                           Paths to directories will serve the HTML page
                           'index.html' or 'index.htm' if contained within
                           directory; otherwise, a directory listing will be
                           served.
[OPTIONS/FLAGS]
--ip ADDRESS ......... Set the server's IPv4 address. Default "127.0.0.1".
-p/--port PORT ....... Set the server's port.         Default "8080".
-h/--help ............ Display this usage message.
--stdin-type ......... Set the MIME type that the standard input will be
                       served as (if "-" is provided as a source).
                       Default "text/plain".
--override-opts ...... Allow the last duplicate of a flag or option to
                       override the first. If not provided, duplicates will
                       causes an error. When provided this flag additionally
                       allows duplicate flags and options to be provided
                       after the sources list.
                           This flag is useful in scripts when you want to
                           use different defaults and still provide the
                           ability to override your new defaults from the
                           command line, when calling the script.
                           E.g. a bash script containing:
                                httpsrvdev --port 9000 $@
                           allows the caller to override the port:
                                $ ./my-script --port 9001
```

Currently under development.

