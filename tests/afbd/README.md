
# hnm

```
tar -xOzf hnm.tgz ./tests.jsonl  > tests.jsonl
solite q "select group_concat(distinct key) from lines_read('tests.jsonl'), json_each(line -> '$.conditions.and[0]')"
```


```
> python test-afbd.py build hnm.tgz --metadata product_group_name,colour_group_name,index_group_name,perceived_colour_value_name,section_name,product_type_name,department_name,graphical_appearance_name,garment_group_name,perceived_colour_master_name
```
