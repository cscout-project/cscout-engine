-- Make resuts repeatable by using integer eids
SELECT
    Row_number() OVER (ORDER BY name) AS eid,
name, readonly, undefmacro, undefedmacro, redefedsamemacro, redefeddiffmacro, macro, funmacro, macroarg, cppconst, cppstrval, defcconstval, notdefcconstval, expcconstval, notexpcconstval, isusedinice, ordinary, suetag, sumember, label, typedef, enum, yacc, fun, cscope, lscope, unused
  FROM ids;
