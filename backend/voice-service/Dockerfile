FROM python:3.11-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY app.py .

ENV PORT=8010
EXPOSE 8010
CMD ["gunicorn", "--workers", "2", "--threads", "4", "--timeout", "180", "--bind", "0.0.0.0:8010", "app:app"]
