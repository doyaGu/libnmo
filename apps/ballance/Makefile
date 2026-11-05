.PHONY: check-style format test

check-style:
	python3 -m ruff check .
	python3 -m ruff format --check .

format:
	python3 -m ruff format .

test:
	python3 -m unittest discover -s tests -v
