To run the example against any available YDB database, you need to know the [endpoint](../../../../../concepts/connect.md#endpoint) and the [database path](../../../../../concepts/connect.md#database).

If authentication is enabled in the database, you also need to select the [authentication mode](../../../../../concepts/auth.md) and get secrets (a token or username/password pair).

Run the command as follows:

```bash
( cd ydb-java-sdk/examples/basic_example/target && \
<auth_mode_var>="<auth_mode_value>" java -jar ydb-basic-example.jar <endpoint>?database=<database>)
```

where

- `<endpoint>`: The [endpoint](../../../../../concepts/connect.md#endpoint).
- `<database>`: The [database path](../../../../../concepts/connect.md#database).
- `<auth_mode_var>`: The [environment variable](../../../auth.md#env) that determines the authentication mode.
- `<auth_mode_value>` is the authentication parameter value for the selected mode.

For example:

```bash
YDB_ACCESS_TOKEN_CREDENTIALS="t1.9euelZqOnJuJlc..." java -jar examples/basic_example/target/ydb-basic-example.jar grpcs://ydb.example.com:2135?database=/somepath/somelocation
```

{% include [../../_includes/pars_from_profile_hint.md](../../_includes/pars_from_profile_hint.md) %}