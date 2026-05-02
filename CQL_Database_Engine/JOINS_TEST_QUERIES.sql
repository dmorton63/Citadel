-- ========================================
-- CQL Database Engine - JOIN Test Suite
-- ========================================
-- Copy and paste these queries one section at a time

-- ========================================
-- SECTION 1: DATABASE SETUP
-- ========================================

-- Create Users table
CREATE TABLE Users (
    Id INT PRIMARY KEY,
    FirstName VARCHAR(50),
    LastName VARCHAR(50),
    Age TINYINT,
    Email VARCHAR(100)
);

-- Insert sample users (IDs 1-20 for comprehensive testing)
INSERT INTO Users VALUES (1, 'Alice', 'Anderson', 28, 'alice.a@email.com');
INSERT INTO Users VALUES (2, 'Bob', 'Brown', 35, 'bob.b@email.com');
INSERT INTO Users VALUES (3, 'Charlie', 'Clark', 22, 'charlie.c@email.com');
INSERT INTO Users VALUES (4, 'Diana', 'Davis', 31, 'diana.d@email.com');
INSERT INTO Users VALUES (5, 'Eve', 'Evans', 27, 'eve.e@email.com');
INSERT INTO Users VALUES (6, 'Frank', 'Foster', 42, 'frank.f@email.com');
INSERT INTO Users VALUES (7, 'Grace', 'Garcia', 19, 'grace.g@email.com');
INSERT INTO Users VALUES (8, 'Henry', 'Harris', 38, 'henry.h@email.com');
INSERT INTO Users VALUES (9, 'Iris', 'Irving', 25, 'iris.i@email.com');
INSERT INTO Users VALUES (10, 'Jack', 'Johnson', 33, 'jack.j@email.com');

-- Create Orders table
CREATE TABLE Orders (
    Id INT PRIMARY KEY,
    UserId INT,
    Product VARCHAR(50),
    Amount REAL
);

-- Insert orders (some users have multiple, some have none)
INSERT INTO Orders VALUES (1, 1, 'Widget', 9.99);
INSERT INTO Orders VALUES (2, 1, 'Gadget', 19.99);
INSERT INTO Orders VALUES (3, 2, 'Tool', 14.99);
INSERT INTO Orders VALUES (4, 2, 'Device', 29.99);
INSERT INTO Orders VALUES (5, 3, 'Component', 7.50);
INSERT INTO Orders VALUES (6, 5, 'Module', 45.00);
INSERT INTO Orders VALUES (7, 10, 'System', 199.99);
INSERT INTO Orders VALUES (8, 10, 'Platform', 299.99);

-- Verify data
SELECT * FROM Users;
SELECT * FROM Orders;

-- ========================================
-- SECTION 2: BASIC INNER JOIN TESTS
-- ========================================

-- Test 1: Basic INNER JOIN (all columns)
SELECT * FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId;

-- Test 2: INNER JOIN with specific columns
SELECT Users.FirstName, Users.LastName, Orders.Product, Orders.Amount
FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId;

-- Test 3: Simple JOIN syntax (defaults to INNER)
SELECT Users.FirstName, Orders.Product
FROM Users JOIN Orders ON Users.Id = Orders.UserId;

-- ========================================
-- SECTION 3: INNER JOIN WITH FILTERS
-- ========================================

-- Test 4: INNER JOIN with WHERE clause
SELECT Users.FirstName, Orders.Product, Orders.Amount
FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId
WHERE Orders.Amount > 20;

-- Test 5: INNER JOIN with ORDER BY
SELECT Users.FirstName, Orders.Product, Orders.Amount
FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId
ORDER BY Orders.Amount DESC;

-- Test 6: INNER JOIN with LIMIT
SELECT Users.FirstName, Orders.Product, Orders.Amount
FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId
ORDER BY Orders.Amount DESC
LIMIT 5;

-- ========================================
-- SECTION 4: LEFT JOIN TESTS
-- ========================================

-- Test 7: Basic LEFT JOIN (shows all users, NULL for users without orders)
SELECT Users.FirstName, Users.LastName, Orders.Product, Orders.Amount
FROM Users LEFT JOIN Orders ON Users.Id = Orders.UserId;

-- Test 8: LEFT JOIN with ORDER BY user name
SELECT Users.FirstName, Users.LastName, Orders.Product, Orders.Amount
FROM Users LEFT JOIN Orders ON Users.Id = Orders.UserId
ORDER BY Users.LastName ASC;

-- ========================================
-- SECTION 5: TABLE ALIASES
-- ========================================

-- Test 9: INNER JOIN with aliases
SELECT u.FirstName, u.LastName, o.Product, o.Amount
FROM Users AS u INNER JOIN Orders AS o ON u.Id = o.UserId;

-- Test 10: LEFT JOIN with aliases and WHERE
SELECT u.FirstName, u.Age, o.Product, o.Amount
FROM Users AS u LEFT JOIN Orders AS o ON u.Id = o.UserId
WHERE u.Age > 25;

-- Test 11: Aliases with WHERE, ORDER BY, and LIMIT
SELECT u.FirstName, u.Email, o.Product, o.Amount
FROM Users AS u INNER JOIN Orders AS o ON u.Id = o.UserId
WHERE o.Amount > 15
ORDER BY o.Amount DESC
LIMIT 10;

-- ========================================
-- SECTION 6: PAGINATION
-- ========================================

-- Test 12: JOIN with OFFSET for pagination
SELECT Users.FirstName, Orders.Product, Orders.Amount
FROM Users INNER JOIN Orders ON Users.Id = Orders.UserId
ORDER BY Orders.Amount ASC
LIMIT 3 OFFSET 2;

-- ========================================
-- SECTION 7: COMPLEX QUERIES
-- ========================================

-- Test 13: Multiple conditions and features
SELECT u.FirstName, u.LastName, u.Age, o.Product, o.Amount
FROM Users AS u INNER JOIN Orders AS o ON u.Id = o.UserId
WHERE u.Age < 30 AND o.Amount > 10
ORDER BY o.Amount DESC
LIMIT 5;

-- ========================================
-- SECTION 8: EDGE CASES
-- ========================================

-- Test 14: User with multiple orders (Alice, Bob, Jack)
SELECT u.FirstName, o.Product, o.Amount
FROM Users AS u INNER JOIN Orders AS o ON u.Id = o.UserId
WHERE u.Id = 1;

-- Test 15: LEFT JOIN showing users WITHOUT orders
SELECT u.FirstName, u.LastName, o.Product
FROM Users AS u LEFT JOIN Orders AS o ON u.Id = o.UserId
ORDER BY u.Id;

-- ========================================
-- END OF TESTS
-- ========================================
